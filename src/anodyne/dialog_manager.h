#pragma once

#include <stdint.h>
#include <stdbool.h>

struct binary_blob
{
    uint32_t mLength;
    uint8_t* mData;
};

struct dialog_manager
{
    struct binary_blob* mData;
};

struct dialog_string
{
    uint32_t mLength;
    const char* mData;
};

typedef enum npc_id
{
    npc_id_empty1
} npc_id;

typedef enum area_id
{
    area_id_empty2
} area_id;

typedef enum scene_id
{
    scene_id_empty3
} scene_id;

bool dialog_manager_init(struct dialog_manager* dm, const struct binary_blob* pData);
void dialog_manager_free(struct dialog_manager* dm);

bool dialog_manager_is_scene_dirty_for_scene(struct dialog_manager* dm, npc_id npc, scene_id scene) ;
bool dialog_manager_is_scene_dirty_for_area(struct dialog_manager* dm, npc_id npc, area_id area, scene_id scene) ;

bool dialog_manager_is_scene_finished_for_scene(struct dialog_manager* dm, npc_id npc, scene_id scene) ;
bool dialog_manager_is_scene_finished_for_area(struct dialog_manager* dm, npc_id npc, area_id area, scene_id scene);

void dialog_manager_set_scene_progress(struct dialog_manager* dm, npc_id npc, scene_id scene, int32_t id);

struct dialog_string* dialog_manager_get(struct dialog_manager* dm, npc_id npc, scene_id scene, int32_t id);
struct dialog_string* dialog_manager_get_for_scene(struct dialog_manager* dm, npc_id npc, area_id area, scene_id scene, int32_t id);

struct dialog_string* dialog_manager_random(struct dialog_manager* dm, npc_id npc, scene_id scene);
struct dialog_string* dialog_manager_random_for_scene(struct dialog_manager* dm, npc_id npc, area_id area, scene_id scene);
