/*++

Copyright (c) 1997 Microsoft Corporation.
All rights reserved.

MODULE NAME:

    kcctask.hxx

ABSTRACT:

    KCC_TASK class.

DETAILS:

    The KCC_TASK class is the base class of all KCC tasks (both periodic and
    notificatication-based).

    It's purpose is to provide a common interface and some common routines
    for all tasks.

CREATED:

    01/21/97    Jeff Parham (jeffparh)

REVISION HISTORY:

--*/

#ifndef KCC_KCCTASK_HXX_INCLUDED
#define KCC_KCCTASK_HXX_INCLUDED

class KCC_TASK : public KCC_OBJECT
{
public:
    // Execute() does the common grunge work of allocating and destroying the
    // thread state, binding to the directory, catching exceptions, etc.
    // All the "real" work is done in ExecuteBody(), which Execute() wraps.
    virtual DWORD
    Execute(
        OUT DWORD * pcSecsUntilNextIteration
        );

    // Initialize the task.  Normally schedules first iteration (if periodic)
    // or constructs notification (if notification-based).
    virtual BOOL Init() = 0;

    // Is this object internally consistent?
    virtual BOOL IsValid() = 0;

    // Execute this task (but serialize it with other tasks running in the task
    // queue, esp. other runs of this same task).
    //
    // Caller may optionally wait for the task to complete (i.e., if the flag
    // DS_KCC_FLAG_ASYNC_OP is specified in dwFlags.
    //
    // If the DS_KCC_FLAG_DAMPED flag is specified, the task will not be
    // triggered if another task is scheduled to run within dwDampedSecs.
    DWORD Trigger(IN DWORD dwFlags, IN DWORD dwDampedSecs);
    
    // Schedule execution of the task.
    BOOL
    Schedule(
        IN  DWORD cSecsUntilFirstIteration
        );

protected:
    // Perform the task.
    virtual
    DWORD
    ExecuteBody(
        OUT DWORD * pcSecsUntilNextIteration
        ) = 0;

    // Given a parameter function name and the parameter block,
    // extract the KCC Task parameter.
    static
    KCC_TASK*
    ExtractTaskFromParam(
        IN  PCHAR   pParamName,
        IN  void*   pParam
        );

    // Compare the parameters of two tasks in the KCC task queue
    // and determine if they match.
    static
    BOOL
    IsMatchedKCCTaskParams(
        IN  PCHAR   pParam1Name,
        IN  void*   param1,
        IN  PCHAR   pParam2Name,
        IN  void*   param2,
        IN  void*   pContext
        );

    // Callback for TaskQueue.  Wraps Execute().
    static
    void
    TaskQueueCallback(
        IN  void *  pvThis,
        OUT void ** ppvThisNext,
        OUT DWORD * pcSecsUntilNextIteration
        );

    // Callback for TaskQueue, when called by a thread that want to wait for
    // completion of the task.
    static
    void
    TriggerCallback(
        IN  void *  pvTriggerInfo,
        OUT void ** ppvNextParam,
        OUT DWORD * pcSecsUntilNext
        );

    // Log task start and end (both normal and abnormal, abnormal being those
    // that are terminated by an exception).
    virtual void LogBegin() = 0;
    virtual void LogEndNormal() = 0;
    virtual void
    LogEndAbnormal(
        IN DWORD dwErrorCode,
        IN DWORD dsid
        ) = 0;
};

#endif
