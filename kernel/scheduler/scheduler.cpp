#include "scheduler.h"
#include "../osData/osData.h"
#include "../memory/heap.h"
#include "../paging/PageFrameAllocator.h"
#include "../paging/PageTableManager.h"
#include "../interrupts/interrupts.h"
#include "../devices/serial/serial.h"
#include "../interrupts/panic.h"
#include "../devices/pit/pit.h"
#include "../rnd/rnd.h"

#include <libm/cstrTools.h>

namespace Scheduler
{
    Lockable<List<osTask*>*> osTasks;
    Lockable<List<void*>*> UsedPageRegions;
    osTask* CurrentRunningTask;
    osTask* NothingDoerTask;
    osTask* DesktopTask;
    bool SchedulerEnabled = false;
    int CurrentTaskIndex = 0;
    void* TestElfFile;
    void* DesktopElfFile;

    void InitScheduler()
    {
        CurrentRunningTask = NULL;
        CurrentTaskIndex = 0;
        NothingDoerTask = NULL;
        DesktopTask = NULL;
        DesktopElfFile = NULL;
        TestElfFile = NULL;

        osTasks = Lockable<List<osTask*>*>(new List<osTask*>());

        UsedPageRegions = Lockable<List<void*>*>(new List<void*>());

        osTasks.Lock();

        Serial::Writelnf("SCHEDULER> INITIALIZING SCHEDULER");

        osTasks.Unlock();

        SchedulerEnabled = false;
    }

    // TODO
    // Handle SMP (Symmetric Multi-Processing) (ye idk not anytime soon prolly)
    // Handle FPU Register States on Context Switch
    interrupt_frame* SchedulerInterrupt(interrupt_frame* currFrame)
    {
        if (!SchedulerEnabled)
            return currFrame;
        if (osTasks.IsLocked())
        {
            CurrentRunningTask = NULL;
            CurrentTaskIndex = 0;
            Serial::Writelnf("SCHEDULER> LOCKED");
            osTasks.Unlock();
            //return currFrame;
        }

        interrupt_frame outFrame = *currFrame;
        
        int64_t time = PIT::TimeSinceBootMS();

        //Serial::Writelnf("SCHEDULER> INTERRUPT %d", osTasks.obj->GetCount());

        osTasks.Lock();

        if (DesktopTask == NULL && DesktopElfFile != NULL)
        {
            Serial::Writelnf("SCHEDULER> CREATING DESKTOP TASK");
            Elf::LoadedElfFile elf = Elf::LoadElf((uint8_t*)DesktopElfFile);
            DesktopTask = CreateTaskFromElf(elf, 0, NULL, false, "bruh:modules/desktop/desktop.elf", "");
            
            osTasks.Unlock();
            AddTask(DesktopTask);
            osTasks.Lock();
        }

        {
            osTask* currentTask = NULL;
            if (CurrentRunningTask != NULL && (int64_t)osTasks.obj->GetCount() > 0 && CurrentTaskIndex <  (int64_t)osTasks.obj->GetCount())
            {
                currentTask = osTasks.obj->ElementAt(CurrentTaskIndex);
                if (currentTask == NULL)
                    Panic("CURRENT TASK IS NULL!", true);
                
                if (currentTask->removeMe || currentTask->doExit)
                {
                    Serial::Writelnf("SCHEDULER> TASK WANTS TO EXIT");
                    //osTasks.obj->RemoveAt(CurrentTaskIndex);
                    CurrentRunningTask = NULL;
                    currentTask = NULL;
                }
                else if (CurrentRunningTask == currentTask)
                {
                    //Serial::Writelnf("SCHEDULER> SAVING PREV DATA");
                    uint64_t tCr3 = currentTask->frame->cr3;
                    *currentTask->frame = *currFrame;
                    currentTask->frame->cr3 = tCr3;
                }
            }
        }

        for (int i = 0; i < osTasks.obj->GetCount(); i++)
        {
            osTask* task = osTasks.obj->ElementAt(i);
            if (task->waitTillMessage && (task->messages->GetCount() != 0 || task->taskTimeoutDone < PIT::TimeSinceBootMS()))
            {
                task->waitTillMessage = false;
                task->taskTimeoutDone = 0;
            }
        }

        for (int i = 0; i < osTasks.obj->GetCount(); i++)
        {
            osTask* bruhTask = osTasks.obj->ElementAt(i);

            if (bruhTask == CurrentRunningTask)
                continue;

            if (bruhTask->removeMe || bruhTask->doExit)
            {
                Serial::Writelnf("SCHEDULER> AUTO REMOVING STOPPED TASK AT %d", i);

                osTasks.Unlock();
                RemoveTask(bruhTask);
                osTasks.Lock();
                i--;
            }
        }

        bool cycleDone = false;
        for (int i = 0; i < osTasks.obj->GetCount(); i++)
        {
            osTask* bruhTask = osTasks.obj->ElementAt(i);

            if (bruhTask->priority == 0)
                continue;
            
            if (bruhTask->removeMe)
                continue;
            
            if (bruhTask->doExit)
                continue;
             
            if (bruhTask->taskTimeoutDone != 0)
                continue;

            if (!bruhTask->active || bruhTask->waitTillMessage)
                continue;

            if (bruhTask->justYielded)
            {
                //bruhTask->justYielded = false;
                continue;
            }

            bruhTask->priorityStep++;
            if (bruhTask->priorityStep >= bruhTask->priority)
            {
                bruhTask->priorityStep = 0;
                CurrentTaskIndex = i;
                cycleDone = true;
                break;
            }
        }

        if (cycleDone)
        {
            cycleDone = false;
            goto skip2ndLoop;
        }
            

        cycleDone = false;
        while (true)
        {
            CurrentTaskIndex++;
            if (CurrentTaskIndex >= osTasks.obj->GetCount())
            {
                if (!cycleDone)
                {
                    CurrentTaskIndex = 0;
                    cycleDone = true;
                }
                else
                    break;
            }

            if (osTasks.obj->GetCount() == 0)
                break;

            osTask* currentTask = osTasks.obj->ElementAt(CurrentTaskIndex);
            if (currentTask->taskTimeoutDone != 0)
                if (currentTask->taskTimeoutDone < time)
                    currentTask->taskTimeoutDone = 0;
            
            if (currentTask->taskTimeoutDone != 0)
                continue;

            if (!currentTask->active || currentTask->waitTillMessage)
                continue;

            if (currentTask->doExit)
                continue;

            if (currentTask->removeMe)
                continue;

            if (currentTask->justYielded)
            {
                //Serial::Writelnf("SCHEDULER> UNYIELDING TASK");
                currentTask->justYielded = false;
                cycleDone = false;
                continue;
            }
            
            if (currentTask->priority != 0)
                continue;

            cycleDone = false;
            break;
        }

        skip2ndLoop:

        osTask* nowTask;
        if (cycleDone)
        {
            if (CurrentRunningTask != NothingDoerTask)
                ;//Serial::Writelnf("SCHEDULER> NO TASKS TO RUN");
            nowTask = NothingDoerTask;

            if (nowTask == NULL)
            {
                osTasks.Unlock();
                Serial::Writelnf("SCHEDULER> NO NOTHING DOER TASK!");
                return currFrame;
            }
        }
        else
            nowTask = osTasks.obj->ElementAt(CurrentTaskIndex);
        

        if (nowTask->doExit || !nowTask->active || nowTask->removeMe)
        {
            Panic("YOU SHOULD NOT BE HERE!!!", true);
            // osTasks.Unlock();
            // Serial::Writelnf("SCHEDULER> TASK EXITED");
            // RemoveTask(currentTask);
            // // free task
            return currFrame;
        }

        //Serial::Writelnf("2> CURR RUNNING TASK: %X, CURR TASK: %X", (uint64_t)currentRunningTask, (uint64_t)currentTask);

        //Serial::Writelnf("SCHEDULER> SWITCHING TO TASK %d", CurrentTaskIndex);

        //Serial::Writelnf("SCHEDULER> LOADING NEXT DATA");
        outFrame = *nowTask->frame;
        if (outFrame.cr3 != (uint64_t)nowTask->pageTableContext)
        {
            Serial::Writelnf("> CR3 MISMATCH! %X != %X", outFrame.cr3, (uint64_t)nowTask->pageTableContext);
            Panic("WAAAAAAAAAAA", true);
        }
        else
            ;//Serial::Writelnf("> CR3 OK");


        if (CurrentRunningTask != nowTask)
        {
            CurrentRunningTask = nowTask;
            if (nowTask != NothingDoerTask)
                ;//Serial::Writelnf("SCHEDULER> SWITCHING TO TASK %d / %d", CurrentTaskIndex, osTasks.obj->GetCount());
            //Serial::Writelnf("SCHEDULER> RIP %X", frame->rip);
        }

        //Serial::Writelnf("> CS 2: %D", frame->cs);

        // set cr3
        //GlobalPageTableManager.SwitchPageTable((PageTable*)currentTask->pageTableContext);
        //frame->cr3 = (uint64_t)((PageTable*)currentTask->pageTableContext)->entries;
        //frame->cr3 = (uint64_t)GlobalPageTableManager.PML4->entries;

        //Serial::Writelnf("SCHEDULER> EXITING INTERRUPT");
        osTasks.Unlock();
        *currFrame = outFrame;
        return currFrame;      
    }

    #define PAGE_REGION_REQUEST_START 0x0000600000000000
    #define PAGE_REGION_REQUEST_STEP   0x0000001000000000

    void* RequestNextFreePageRegion()
    {
        UsedPageRegions.Lock();

        void* addr = (void*)PAGE_REGION_REQUEST_START;
        while (UsedPageRegions.obj->GetIndexOf(addr) != -1)
            addr = (void*)((uint64_t)addr + PAGE_REGION_REQUEST_STEP);

        UsedPageRegions.obj->Add(addr);
        UsedPageRegions.Unlock();
        return addr;
    }

    void FreePageRegion(void* addr)
    {
        UsedPageRegions.Lock();
        int index = UsedPageRegions.obj->GetIndexOf(addr);
        if (index != -1)
            UsedPageRegions.obj->RemoveAt(index);
        UsedPageRegions.Unlock();
    }

    osTask* CreateTaskFromElf(Elf::LoadedElfFile module, int argC, const char** argV, bool isUserMode, const char* elfPath, const char* startedAtPath)
    {
        bool tempEnabled = SchedulerEnabled;
        SchedulerEnabled = false;

        AddToStack();
        osTask* task = new osTask();
        RemoveFromStack();

        uint8_t* kernelStack = (uint8_t*)GlobalAllocator->RequestPages(KERNEL_STACK_PAGE_SIZE);
        GlobalPageTableManager.MapMemories(kernelStack + MEM_AREA_TASK_KERNEL_STACK_OFFSET, kernelStack, KERNEL_STACK_PAGE_SIZE, PT_Flag_Present | PT_Flag_ReadWrite);
        kernelStack += MEM_AREA_TASK_KERNEL_STACK_OFFSET;

        uint8_t* userStack = (uint8_t*)GlobalAllocator->RequestPages(USER_STACK_PAGE_SIZE);
        if (isUserMode)
            GlobalPageTableManager.MapMemories(userStack + MEM_AREA_TASK_USER_STACK_OFFSET, userStack, USER_STACK_PAGE_SIZE, PT_Flag_Present | PT_Flag_ReadWrite | PT_Flag_UserSuper);
        else
            GlobalPageTableManager.MapMemories(userStack + MEM_AREA_TASK_USER_STACK_OFFSET, userStack, USER_STACK_PAGE_SIZE, PT_Flag_Present | PT_Flag_ReadWrite);
        userStack += MEM_AREA_TASK_USER_STACK_OFFSET;


        _memset(kernelStack, 0, KERNEL_STACK_PAGE_SIZE * 0x1000);
        _memset(userStack, 0, USER_STACK_PAGE_SIZE * 0x1000);
        
        task->kernelStack = kernelStack;
        task->userStack = userStack;

        
        task->taskTimeoutDone = 0;
        AddToStack();
        task->requestedPages = new List<void*>(4);
        task->messages = new Queue<GenericMessagePacket*>(4);
        RemoveFromStack();
        task->doExit = false;
        task->active = true;
        task->priority = 0;
        task->priorityStep = 0;
        task->isKernelModule = !isUserMode;
        task->justYielded = false;
        task->removeMe = false;
        task->elfFile = module;
        task->pid = RND::RandomInt();
        task->parentPid = 0;
        task->addrOfVirtPages = RequestNextFreePageRegion();
        task->elfPath = StrCopy(elfPath);
        task->startedAtPath = StrCopy(startedAtPath);
        Serial::Writelnf("SCHEDULER> Creating Task with PID: %D", task->pid);

        {
            task->argC = argC;
            task->argV = (const char**)_Malloc(sizeof(const char*) * argC);
            for (int i = 0; i < argC; i++)
                task->argV[i] = StrCopy(argV[i]);
        }

        task->pageTableContext = GlobalPageTableManager.CreatePageTableContext();
        PageTableManager tempManager = PageTableManager((PageTable*)task->pageTableContext);
        Serial::Writelnf("SCHEDULER> Creating Page Table Context at %X (%X)", task->pageTableContext, tempManager.PML4);
        


        if (!isUserMode)
            ;
        CopyPageTable(GlobalPageTableManager.PML4, tempManager.PML4);

        if (isUserMode)
            tempManager.MapMemories(userStack, userStack - MEM_AREA_TASK_USER_STACK_OFFSET, USER_STACK_PAGE_SIZE, PT_Flag_Present | PT_Flag_ReadWrite | PT_Flag_UserSuper);
        else
            tempManager.MapMemories(userStack, userStack - MEM_AREA_TASK_USER_STACK_OFFSET, USER_STACK_PAGE_SIZE, PT_Flag_Present | PT_Flag_ReadWrite);


        uint8_t* kernelStackEnd = kernelStack + KERNEL_STACK_PAGE_SIZE * 0x1000;
        uint8_t* userStackEnd = userStack + USER_STACK_PAGE_SIZE * 0x1000;

        kernelStackEnd -= sizeof(interrupt_frame);
        interrupt_frame* frame = (interrupt_frame*)kernelStackEnd;
        _memset(frame, 0, sizeof(interrupt_frame));
        task->frame = frame;

        userStackEnd -= 100*sizeof(uint64_t) + sizeof(interrupt_frame);
        kernelStackEnd -= 100*sizeof(uint64_t) + sizeof(interrupt_frame);

        //task->pageTableContext = (void*)GlobalPageTableManager.PML4;

        if (isUserMode)
        {
            frame->rip = (uint64_t)module.entryPoint;
            frame->cr3 = (uint64_t)tempManager.PML4;
            frame->cr0 = 0x80000000;
            
            //frame->cr0 = (uint64_t)GlobalPageTableManager.PML4 | 0x80000000;
            frame->rsp = (uint64_t)userStackEnd;
            //frame->rbp = (uint64_t)userStackEnd - 0x2000;
            //frame->rax = (uint64_t)0;
            frame->cs = 0x28 | 0x03;
            frame->ss = 0x20 | 0x03;

            frame->rflags = 0x202;
        }
        else
        {
            frame->rip = (uint64_t)module.entryPoint;
            frame->cr3 = (uint64_t)tempManager.PML4;
            frame->cr0 = 0x80000000;
            //frame->cr0 = (uint64_t)GlobalPageTableManager.PML4 | 0x80000000;
            frame->rsp = (uint64_t)userStackEnd;
            //frame->rbp = (uint64_t)userStackEnd - 0x2000;
            //frame->rax = (uint64_t)module.entryPoint;
            frame->cs = 0x8;
            frame->ss = 0x10;

            frame->rflags = 0x202;
        }

        Serial::Writelnf("> Started Task with addr %X", task);

        SchedulerEnabled = tempEnabled;
        return task;
    }

    void AddTask(osTask* task)
    {
        bool tempEnabled = SchedulerEnabled;
        SchedulerEnabled = false;
        
        osTasks.Lock();
        osTasks.obj->Add(task);
        osTasks.Unlock();

        SchedulerEnabled = tempEnabled;
    }

    void RemoveTask(osTask* task)
    {
        bool tempEnabled = SchedulerEnabled;
        SchedulerEnabled = false;
        AddToStack();

        if (task == NULL)
        {
            Panic("Trying to remove non-existant Task!", true);
            RemoveFromStack();
            return;
        }

        AddToStack();
        task->active = false;
        RemoveFromStack();

        AddToStack();
        osTasks.Lock();
        RemoveFromStack();

        //Serial::Writelnf("> Trying to remove task at %X", (uint64_t)task);

        AddToStack();
        int index = osTasks.obj->GetIndexOf(task);
        RemoveFromStack();
        if (index != -1)
        {
            AddToStack();
            //Serial::Writelnf("> Removing task at index %d", index);
            osTasks.obj->RemoveAt(index);
            //Serial::Writelnf("> Removed task at index %d", index);
            RemoveFromStack();
        }

        if (task->argV != NULL)
        {
            for (int i = 0; i < task->argC; i++)
            {
                AddToStack();
                _Free((void*)task->argV[i]);
                RemoveFromStack();
            }

            AddToStack();
            _Free(task->argV);
            RemoveFromStack();
        }

        // free task
        AddToStack();
        if (task->requestedPages != NULL)
        {
            AddToStack();
            for (int i = 0; i < task->requestedPages->GetCount(); i++)
            {
                //Serial::Writelnf("> Freeing page %X", (uint64_t)task->requestedPages->ElementAt(i));
                GlobalAllocator->FreePage((void*)task->requestedPages->ElementAt(i));
            }
            RemoveFromStack();

            AddToStack();
            //Serial::Writelnf("> Freeing requested pages");
            task->requestedPages->Free();
            _Free(task->requestedPages);
            task->requestedPages = NULL;
            RemoveFromStack();
        }
        RemoveFromStack();

        AddToStack();
        if (task->kernelStack != NULL)
        {
            //Serial::Writelnf("> Freeing kernel stack");
            GlobalAllocator->FreePages(task->kernelStack - MEM_AREA_TASK_KERNEL_STACK_OFFSET, KERNEL_STACK_PAGE_SIZE);
            task->kernelStack = NULL;
        }
        RemoveFromStack();

        AddToStack();
        if (task->userStack != NULL)
        {
            //Serial::Writelnf("> Freeing user stack");
            GlobalAllocator->FreePages(task->userStack - MEM_AREA_TASK_USER_STACK_OFFSET, USER_STACK_PAGE_SIZE);
            task->userStack = NULL;
        }
        RemoveFromStack();

        AddToStack();
        if (task->pageTableContext != NULL)
        {
            //Serial::Writelnf("> Freeing page table");
            GlobalPageTableManager.FreePageTable((PageTable*)task->pageTableContext);
            task->pageTableContext = NULL;
        }
        RemoveFromStack();

        AddToStack();
        if (task->messages != NULL)
        {
            while (task->messages->GetCount() > 0)
            {
                GenericMessagePacket* msg = task->messages->Dequeue();
                if (msg == NULL)
                    continue;
                msg->Free();
                _Free(msg);
            }
            task->messages->Free();
            _Free(task->messages);
            task->messages = NULL;
        }
        RemoveFromStack();

        AddToStack();
        if (task->elfPath != NULL)
        {
            _Free((void*)task->elfPath);
            task->elfPath = NULL;
        }
        RemoveFromStack();

        AddToStack();
        if (task->startedAtPath != NULL)
        {
            _Free((void*)task->startedAtPath);
            task->startedAtPath = NULL;
        }
        RemoveFromStack();

        {
            Elf::FreeElf(task->elfFile);
        }

        AddToStack();
        FreePageRegion(task->addrOfVirtPages);
        RemoveFromStack();

        AddToStack();
        osTasks.Unlock();
        RemoveFromStack();

        if (task == DesktopTask)
        {
            DesktopTask = NULL;
        }

        AddToStack();
        //Serial::Writelnf("> Freeing task");
        _Free(task);
        RemoveFromStack();

        if (task == CurrentRunningTask)
            CurrentRunningTask = NULL;

        RemoveFromStack();
        SchedulerEnabled = tempEnabled;
        //Serial::Writelnf("> Done");
    }

    osTask* GetTask(uint64_t pid)
    {
        bool tempEnabled = SchedulerEnabled;
        SchedulerEnabled = false;

        osTasks.Lock();
        for (int i = 0; i < osTasks.obj->GetCount(); i++)
        {
            osTask* task = osTasks.obj->ElementAt(i);
            if (task->pid == pid)
            {
                osTasks.Unlock();
                SchedulerEnabled = tempEnabled;
                return task;
            }
        }
        osTasks.Unlock();
        SchedulerEnabled = tempEnabled;
        return NULL;
    }

}

