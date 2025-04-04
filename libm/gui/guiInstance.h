#pragma once
//#include "../../../osData/userData.h"
//#include "../defaultInstance/defaultInstance.h"
//#include "../../Window/window.h"
//#include "../../../tasks/task.h"
#include <libm/window/window.h>


class GuiInstance;

#include "guiStuff/components/screenComponent/screenComponent.h"



enum class GuiInstanceBaseAttributeType : int32_t
{   
    POSITION_X = 10,
    POSITION_Y = 11,

    SIZE_FIXED_X = 20,
    SIZE_FIXED_Y = 21,
    SIZE_SCALED_X = 22,
    SIZE_SCALED_Y = 23,
    SIZE_IS_FIXED_X = 24,
    SIZE_IS_FIXED_Y = 25,

    ACTUAL_SIZE_X = 30,
    ACTUAL_SIZE_Y = 31,

    ID = 40, 
    PARENT_ID = 41,
    IS_HIDDEN = 42,

};

enum InstanceType
{
    Default,
    Terminal,
    DebugTerminal,
    NewTerminal,
    Connect4,
    GUI,
    CRASH,
    WARNING,
    TESTO_PGM
};

#include <libm/mouseState.h>

class GuiInstance// : public DefaultInstance
{
    private:
        Window* window;
        

    public:
    InstanceType instanceType = InstanceType::Default;
    void* audioSource = NULL;
    void (*FreeFunc)(void* bruh) = NULL;
    void DefaultFree();


    GuiComponentStuff::ScreenComponent* screen;
    List<GuiComponentStuff::BaseComponent*>* allComponents;
    
    // Task* waitTask;
    // Task* waitTask2;
    // bool waitingForTask;
    bool oldResizeable;
    // void* OnWaitTaskDoneHelp;
    // void (*OnWaitTaskDoneCallback)(void* bruh, Task* tsk);

    MouseState mouseState = MouseState(0, 0, false, false, false);

    GuiInstance(Window* window);
    void Free();
    void Init();
    void Render(bool update);
    void Update();

    GuiComponentStuff::BaseComponent* GetComponentFromId(uint64_t id);
    GuiComponentStuff::BaseComponent* GetChildFromComponentWithId(uint64_t id, int index);
    int GetIndexOfChildFromComponentWithId(uint64_t id, uint64_t childId);
    bool RemoveChildFromComponentWithId(uint64_t id, int index);

    bool DeleteComponentWithId(int64_t id, bool destroyChildren);
    bool CreateComponentWithId(int64_t id, GuiComponentStuff::ComponentType type);
    bool CreateComponentWithIdAndParent(int64_t id, GuiComponentStuff::ComponentType type, int64_t parentId);
    
    bool SetBaseComponentAttribute(int64_t id, GuiInstanceBaseAttributeType type, uint64_t val);
    bool SetSpecificComponentAttribute(int64_t id, int32_t type, uint64_t val);



    uint64_t GetBaseComponentAttribute(int64_t id, GuiInstanceBaseAttributeType type);
    uint64_t GetSpecificComponentAttribute(int64_t id, int32_t type);
    int GetSpecificComponentAttributeSize(int64_t id, int32_t type);

    bool SetActiveScreenFromId(int64_t id);

    bool ComponentAddChild(int64_t id, GuiComponentStuff::BaseComponent* childComp);
    bool ComponentRemoveChild(int64_t id, int64_t childId);



};

int GetBaseComponentAttributeSize(GuiInstanceBaseAttributeType type);