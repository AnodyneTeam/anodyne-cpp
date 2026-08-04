#include "dialog_manager.h"
#include <stdlib.h>


bool dialog_manager_init(struct dialog_manager* dm, const struct binary_blob* pData)
{
    return true;
}

void dialog_manager_free(struct dialog_manager* dm)
{

}

bool dialog_manager_is_scene_dirty_for_scene(struct dialog_manager* dm, npc_id npc, scene_id scene)
{
    return false;
}

bool dialog_manager_is_scene_dirty_for_area(struct dialog_manager* dm, npc_id npc, area_id area, scene_id scene)
{
    return false;
}

bool dialog_manager_is_scene_finished_for_scene(struct dialog_manager* dm, npc_id npc, scene_id scene)
{
    return false;
}

bool dialog_manager_is_scene_finished_for_area(struct dialog_manager* dm, npc_id npc, area_id area, scene_id scene)
{
    return false;
}

void dialog_manager_set_scene_progress(struct dialog_manager* dm, npc_id npc, scene_id scene, int32_t id)
{

}

struct dialog_string* dialog_manager_get(struct dialog_manager* dm, npc_id npc, scene_id scene, int32_t id)
{
    return NULL;
}

struct dialog_string* dialog_manager_get_for_scene(struct dialog_manager* dm, npc_id npc, area_id area, scene_id scene, int32_t id)
{
    return NULL;
}

struct dialog_string* dialog_manager_random(struct dialog_manager* dm, npc_id npc, scene_id scene)
{
    return NULL;
}

struct dialog_string* dialog_manager_random_for_scene(struct dialog_manager* dm, npc_id npc, area_id area, scene_id scene)
{
    return NULL;
}

