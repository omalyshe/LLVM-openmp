//******************************************************************************
// include files
//******************************************************************************

#include "kmp.h"
#include "ompt-internal.h"
#include "ompt-specific.h"

//******************************************************************************
// macros
//******************************************************************************

#define LWT_FROM_TEAM(team) (team)->t.ompt_serialized_team_info;

#define OMPT_THREAD_ID_BITS 16

// 2013 08 24 - John Mellor-Crummey
//   ideally, a thread should assign its own ids based on thread private data.
//   however, the way the intel runtime reinitializes thread data structures
//   when it creates teams makes it difficult to maintain persistent thread
//   data. using a shared variable instead is simple. I leave it to intel to
//   sort out how to implement a higher performance version in their runtime.

// when using fetch_and_add to generate the IDs, there isn't any reason to waste
// bits for thread id.
#if 0
#define NEXT_ID(id_ptr,tid) \
  ((KMP_TEST_THEN_INC64(id_ptr) << OMPT_THREAD_ID_BITS) | (tid))
#else
#define NEXT_ID(id_ptr,tid) (KMP_TEST_THEN_INC64((volatile kmp_int64 *)id_ptr))
#endif

//******************************************************************************
// private operations
//******************************************************************************

//----------------------------------------------------------
// traverse the team and task hierarchy
// note: __ompt_get_teaminfo and __ompt_get_taskinfo
//       traverse the hierarchy similarly and need to be
//       kept consistent
//----------------------------------------------------------

ompt_team_info_t *
__ompt_get_teaminfo(int depth, int *size)
{
    kmp_info_t *thr = ompt_get_thread();

    if (thr) {
        kmp_team *team = thr->th.th_team;
        if (team == NULL) return NULL;

        ompt_lw_taskteam_t *lwt = LWT_FROM_TEAM(team);

        while(depth > 0) {
            // next lightweight team (if any)
            if (lwt) lwt = lwt->parent;

            // next heavyweight team (if any) after
            // lightweight teams are exhausted
            if (!lwt && team) team=team->t.t_parent;

            depth--;
        }

        if (lwt) {
            // lightweight teams have one task
            if (size) *size = 1;

            // return team info for lightweight team
            return &lwt->ompt_team_info;
        } else if (team) {
            // extract size from heavyweight team
            if (size) *size = team->t.t_nproc;

            // return team info for heavyweight team
            return &team->t.ompt_team_info;
        }
    }

    return NULL;
}


ompt_task_info_t *
__ompt_get_taskinfo(int depth)
{
    ompt_task_info_t *info = NULL;
    kmp_info_t *thr = ompt_get_thread();

    if (thr) {
        kmp_taskdata_t  *taskdata = thr->th.th_current_task;
        ompt_lw_taskteam_t *lwt = LWT_FROM_TEAM(taskdata->td_team);

        while (depth > 0) {
            // next lightweight team (if any)
            if (lwt) lwt = lwt->parent;

            // next heavyweight team (if any) after
            // lightweight teams are exhausted
            if (!lwt && taskdata) {
                taskdata = taskdata->td_parent;
                if (taskdata) {
                    lwt = LWT_FROM_TEAM(taskdata->td_team);
                }
            }
            depth--;
        }

        if (lwt) {
            info = &lwt->ompt_task_info;
        } else if (taskdata) {
            info = &taskdata->ompt_task_info;
        }
    }

    return info;
}

ompt_task_info_t *
__ompt_get_scheduling_taskinfo(int depth)
{
    ompt_task_info_t *info = NULL;
    kmp_info_t *thr = ompt_get_thread();

    if (thr) {
        kmp_taskdata_t  *taskdata = thr->th.th_current_task;
//        ompt_lw_taskteam_t *lwt = LWT_FROM_TEAM(taskdata->td_team);

        while (depth > 0) {

            if (taskdata) {
                taskdata = taskdata->ompt_task_info.scheduling_parent;
            }else{
                return NULL;
            }
            depth--;
        }

        info = &taskdata->ompt_task_info;
    }

    return info;
}



//******************************************************************************
// interface operations
//******************************************************************************

//----------------------------------------------------------
// thread support
//----------------------------------------------------------

ompt_id_t
__ompt_thread_id_new()
{
    static uint64_t ompt_thread_id = 1;
    return NEXT_ID(&ompt_thread_id, 0);
}

ompt_thread_data_t *
__ompt_get_thread_data_internal()
{
    kmp_info_t *thread = ompt_get_thread();
    return &(thread->th.ompt_thread_info.thread_data);
}

//----------------------------------------------------------
// state support
//----------------------------------------------------------

void
__ompt_thread_assign_wait_id(void *variable)
{
    kmp_info_t *ti = ompt_get_thread();

    ti->th.ompt_thread_info.wait_id = (ompt_wait_id_t) variable;
}

ompt_state_t
__ompt_get_state_internal(ompt_wait_id_t *ompt_wait_id)
{
    kmp_info_t *ti = ompt_get_thread();

    if (ti) {
        if (ompt_wait_id)
            *ompt_wait_id = ti->th.ompt_thread_info.wait_id;
        return ti->th.ompt_thread_info.state;
    }
    return ompt_state_undefined;
}

//----------------------------------------------------------
// idle frame support
//----------------------------------------------------------

void *
__ompt_get_idle_frame_internal(void)
{
    kmp_info_t *ti = ompt_get_thread();
    return ti ? ti->th.ompt_thread_info.idle_frame : NULL;
}


//----------------------------------------------------------
// parallel region support
//----------------------------------------------------------

ompt_id_t
__ompt_parallel_id_new(int gtid)
{
#ifdef OMPT_INIT_IDS
    static uint64_t ompt_parallel_id = 1;
    return gtid >= 0 ? NEXT_ID(&ompt_parallel_id, gtid) : 0;
#else
    return 0;
#endif
}


void *
__ompt_get_parallel_function_internal(int depth)
{
    ompt_team_info_t *info = __ompt_get_teaminfo(depth, NULL);
    void *function = info ? info->microtask : NULL;
    return function;
}


ompt_parallel_data_t
__ompt_get_parallel_data_internal(int depth)
{
    ompt_team_info_t *info = __ompt_get_teaminfo(depth, NULL);
    ompt_parallel_data_t id = ompt_parallel_id_none;
    return info ? info->parallel_data :id;
}


int
__ompt_get_parallel_team_size_internal(int depth)
{
    // initialize the return value with the error value.
    // if there is a team at the specified depth, the default
    // value will be overwritten the size of that team.
    int size = -1;
    (void) __ompt_get_teaminfo(depth, &size);
    return size;
}


//----------------------------------------------------------
// lightweight task team support
//----------------------------------------------------------

void
__ompt_lw_taskteam_init(ompt_lw_taskteam_t *lwt, kmp_info_t *thr,
                        int gtid, void *microtask,
                        ompt_parallel_data_t ompt_pid)
{
    lwt->ompt_team_info.parallel_data = ompt_pid;
    lwt->ompt_team_info.microtask = microtask;
    lwt->ompt_task_info.task_data.value = 0;
    lwt->ompt_task_info.frame.reenter_runtime_frame = 0;
    lwt->ompt_task_info.frame.exit_runtime_frame = 0;
    lwt->ompt_task_info.function = NULL;
    lwt->parent = 0;
}


void
__ompt_lw_taskteam_link(ompt_lw_taskteam_t *lwt,  kmp_info_t *thr)
{
    ompt_lw_taskteam_t *my_parent = thr->th.th_team->t.ompt_serialized_team_info;
    lwt->parent = my_parent;
    thr->th.th_team->t.ompt_serialized_team_info = lwt;
}


ompt_lw_taskteam_t *
__ompt_lw_taskteam_unlink(kmp_info_t *thr)
{
    ompt_lw_taskteam_t *lwtask = thr->th.th_team->t.ompt_serialized_team_info;
    if (lwtask) thr->th.th_team->t.ompt_serialized_team_info = lwtask->parent;
    return lwtask;
}


//----------------------------------------------------------
// task support
//----------------------------------------------------------

ompt_id_t
__ompt_task_id_new(int gtid)
{
#ifdef OMPT_INIT_IDS
    static uint64_t ompt_task_id = 1;
    return NEXT_ID(&ompt_task_id, gtid);
#else
    return 0;
#endif
}


ompt_task_data_t
__ompt_get_task_data_internal(int depth)
{
    ompt_task_info_t *info = __ompt_get_scheduling_taskinfo(depth);
    ompt_task_data_t task_data = ompt_task_id_none;
    return info ?  info->task_data : task_data;
}


void *
__ompt_get_task_function_internal(int depth)
{
    ompt_task_info_t *info = __ompt_get_scheduling_taskinfo(depth);
    void *function = info ? info->function : NULL;
    return function;
}


// OpenMP spec asks for the scheduling task to be returned.

ompt_frame_t *
__ompt_get_task_frame_internal(int depth)
{
    ompt_task_info_t *info = __ompt_get_scheduling_taskinfo(depth);
    ompt_frame_t *frame = info ? frame = &info->frame : NULL;
    return frame;
}

//----------------------------------------------------------
// team support
//----------------------------------------------------------

void
__ompt_team_assign_id(kmp_team_t *team, ompt_parallel_data_t ompt_pid)
{
    team->t.ompt_team_info.parallel_data = ompt_pid;
}
