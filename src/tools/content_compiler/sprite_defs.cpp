// Sprite sheet / animation definitions, transcribed from AnodyneSharp (the C# builds these inline in each
// entity's AnimatedSpriteRenderer/StaticSpriteRenderer constructor call). Texture names are the PNG file
// stems under old/Content/textures.
//
// Hand-editable: the content compiler (sprite_compiler.cpp) converts exactly the sheets listed here.
//  - Anims whose frames were "base + offset" in the C# (per-variant / per-instance offsets such as Rat's `o`)
//    are recorded for offset 0 and tagged "variant-relative"; the runtime adds the instance offset.
//  - A "name@N" anim is a second, different anim of the same name the C# builds for the same sheet.
//  - Anim names are as in the C#; fps is the C# frame rate (frames/second, 0 = static).
#include "sprite_compiler.hpp"

const std::vector<SpriteSheetSpec>& GetSpriteSheetSpecs()
{
    static const std::vector<SpriteSheetSpec> kSpriteSheets =
{
    { "10x28_shadow", 10, 28, {
    } },
    { "28x10_shadow", 28, 10, {
    } },
    { "5-frame-glow", 48, 48, {
        { "glow", { 0, 0, 1, 2, 3, 4, 3, 2, 1, 0, 0, 0 }, 7.0f, true },
    } },
    { "5-frame-glow-bright", 48, 48, {
    } },
    { "8x8_shadow", 8, 8, {
    } },
    { "DustUI", 9, 9, {
        { "default", { 4 }, 1.0f, true },
        { "empty", { 0, 1, 2, 3, 4 }, 13.0f, false },
        { "full", { 4, 3, 2, 1, 0 }, 13.0f, false },
    } },
    { "TREE", 64, 64, {
    } },
    { "annoyer", 16, 16, {
        { "flap", { 0, 1, 2, 3, 4, 5 }, 8.0f, true },
        { "flap_super", { 12, 13, 14, 15, 16, 17 }, 8.0f, true },
        { "flap@cell", { 6, 7 }, 4.0f, true },
    } },
    { "arrows", 7, 7, {
        { "disabledRight", { 2 }, 1.0f, true },
        { "disabledLeft", { 0 }, 1.0f, true },
        { "enabledLeft", { 0, 1 }, 6.0f, true },
        { "enabledRight", { 3, 2 }, 6.0f, true },
    } },
    { "arthur", 16, 16, {
        { "walk_d", { 0, 1 }, 8.0f, true },
        { "walk_l", { 4, 5 }, 8.0f, true },
        { "walk_u", { 2, 3 }, 8.0f, true },
        { "walk_r", { 4, 5 }, 8.0f, true },
        { "roll", { 6 }, 6.0f, true },
        { "stunned", { 8, 9 }, 6.0f, true },
        { "wobble", { 16, 17 }, 8.0f, true },
        { "fall_1", { 10 }, 8.0f, true },
        { "fall", { 10, 11, 12, 13, 14, 15, 6 }, 2.0f, false },
        { "pre-fall", { 1 }, 1.0f, true },
    } },
    { "arthur_javiera", 16, 32, {
        { "walk_d", { 0, 1 }, 8.0f, true },
        { "walk_u", { 2, 3 }, 12.0f, true },
        { "walk_r", { 4, 5 }, 12.0f, true },
        { "walk_l", { 4, 5 }, 12.0f, true },
        { "switch", { 6, 7 }, 6.0f, false },
        { "throw_d", { 8, 8 }, 2.0f, false },
        { "throw_r", { 10, 8 }, 2.0f, false },
        { "throw_u", { 9, 8 }, 2.0f, false },
        { "throw_l", { 10, 8 }, 2.0f, false },
        { "dying", { 10, 10, 10 }, 1.0f, false },
    } },
    { "autosave_icon", 64, 16, {
        { "a", { 0, 1, 2, 3, 4, 5 }, 8.0f, true },
    } },
    { "beach_npcs", 16, 16, {
        { "idle", { 10, 11 }, 3.0f, true },
        { "dead", { 12 }, 1.0f, true },
        { "a", { 0 }, 1.0f, true },
        { "turn", { 1 }, 1.0f, true },
    } },
    { "big_door", 32, 32, {
    } },
    { "big_health_pickup", 16, 16, {
        { "float", { 0, 1, 2, 3 }, 5.0f, true },  // variant-relative
    } },
    { "big_statue", 32, 48, {
    } },
    { "bike", 20, 20, {
        { "r", { 1 }, 1.0f, true },
    } },
    { "biofilm", 32, 32, {
    } },
    { "blue_npcs", 16, 16, {
        { "idle", { 10 }, 1.0f, true },  // variant-relative
        { "fall", { 10, 11, 12, 13, 14 }, 7.0f, false },  // variant-relative
        { "normal", { 0 }, 0.0f, false },
        { "melt", { 2, 3, 4, 5, 6 }, 7.0f, false },
        { "melted", { 6 }, 1.0f, true },
    } },
    { "briar", 16, 16, {
        { "idle", { 0 }, 1.0f, true },
        { "walk_d", { 0, 1 }, 4.0f, true },
        { "walk_r", { 2, 3 }, 4.0f, true },
        { "walk_u", { 4, 5 }, 4.0f, true },
        { "walk_l", { 6, 7 }, 4.0f, true },
        { "oh_no", { 10 }, 1.0f, true },
        { "idle_d", { 20 }, 12.0f, true },
        { "idle_l", { 26 }, 12.0f, true },
        { "idle_r", { 22 }, 12.0f, true },
        { "idle_u", { 24 }, 12.0f, true },
        { "walk_d@2", { 20, 21 }, 4.0f, true },
        { "walk_l@2", { 26, 27 }, 4.0f, true },
        { "walk_r@2", { 22, 23 }, 4.0f, true },
        { "walk_u@2", { 24, 25 }, 4.0f, true },
    } },
    { "briar_arm_left", 64, 80, {
        { "off", { 0, 1, 2, 3 }, 4.0f, true },  // variant-relative
        { "hit", { 4, 6, 4, 6, 4, 6, 4, 6, 4, 6, 4, 6 }, 15.0f, false },
        { "hitloop", { 4, 6 }, 15.0f, true },
        { "hurt", { 7, 8 }, 4.0f, true },
        { "active", { 4, 5 }, 5.0f, true },
    } },
    { "briar_arm_right", 64, 80, {
        { "off", { 1, 2, 3, 0 }, 4.0f, true },
        { "hit", { 4, 6, 4, 6, 4, 6, 4, 6, 4, 6, 4, 6 }, 15.0f, false },
        { "hitloop", { 4, 6 }, 15.0f, true },
        { "hurt", { 7, 8 }, 4.0f, true },
        { "active", { 4, 5 }, 5.0f, true },
    } },
    { "briar_body", 128, 80, {
    } },
    { "briar_core", 32, 18, {
        { "glow", { 0, 1, 2, 1 }, 4.0f, true },
        { "flash", { 3, 4 }, 12.0f, true },
    } },
    { "briar_dust_explosion", 48, 48, {
        { "explode", { 0, 1, 2, 3, 4 }, 12.0f, false },
    } },
    { "briar_fire_eye", 16, 16, {
        { "shoot", { 6, 7, 8, 9 }, 10.0f, true },
        { "grow", { 0, 1, 2, 3, 4, 5 }, 10.0f, false },
        { "ungrow", { 5, 4, 3, 2, 1, 0 }, 10.0f, false },
        { "move", { 12, 13 }, 12.0f, true },
    } },
    { "briar_ground_thorn", 16, 16, {
        { "attack", { 0, 1, 2, 0, 3, 3, 3, 6, 7, 8, 9, 10, 9, 10, 9, 10, 8, 7, 6 }, 8.0f, false },
        { "a", { 9, 10 }, 8.0f, true },
    } },
    { "briar_ground_thorn", 32, 16, {
        { "move", { 6, 7, 8 }, 6.0f, true },
    } },
    { "briar_ice_crystal", 16, 16, {
        { "move", { 0, 1 }, 6.0f, true },
    } },
    { "briar_ice_explosion", 24, 24, {
        { "explode", { 0, 1, 2, 3 }, 15.0f, false },
    } },
    { "briar_mist", 24, 24, {
        { "a", { 0, 1 }, 5.0f, true },
    } },
    { "briar_overhang", 160, 48, {
    } },
    { "briar_thorn_bullet", 16, 16, {
        { "move", { 0, 1 }, 10.0f, true },
    } },
    { "broom_reflection", 16, 16, {
    } },
    { "broom", 16, 16, {
        { "stab", { 1, 2, 2, 1, 0, 0 }, 20.0f, false },
    } },
    { "broom_cell", 16, 16, {
        { "stab", { 1, 2, 2, 1, 0, 0 }, 20.0f, false },
    } },
    { "burst_plant", 16, 16, {
        { "idle", { 0 }, 1.0f, true },
        { "charging", { 0, 1 }, 8.0f, true },
        { "shoot", { 3 }, 1.0f, true },
    } },
    { "burst_plant_bullet", 8, 8, {
        { "move", { 0, 1 }, 12.0f, true },
        { "explode", { 2, 3, 4, 4 }, 10.0f, false },
    } },
    { "buttons", 16, 16, {
    } },
    { "cell_bodies", 16, 16, {
        { "idle_d", { 8 }, 1.0f, true },
        { "idle_r", { 9 }, 1.0f, true },
        { "idle_u", { 10 }, 1.0f, true },
        { "idle_l", { 11 }, 1.0f, true },
    } },
    { "chaser", 16, 32, {
        { "face_d", { 4 }, 1.0f, true },
        { "face_r", { 6 }, 1.0f, true },
        { "walking_d", { 4, 5 }, 8.0f, true },
        { "walking_r", { 6, 7 }, 8.0f, true },
        { "walking_u", { 8, 9 }, 8.0f, true },
        { "walking_l", { 10, 11 }, 8.0f, true },
    } },
    { "checkbox", 11, 11, {
    } },
    { "checkpoint", 16, 16, {
        { "inactive", { 0 }, 1.0f, true },  // variant-relative
        { "active", { 1, 2, 3 }, 10.0f, false },  // variant-relative
        { "stepped_on", { 1, 2 }, 12.0f, true },  // variant-relative
    } },
    { "cliffs_npcs", 16, 16, {
        { "a", { 0 }, 1.0f, true },
        { "move", { 1, 3, 1, 5, 1, 1, 1, 0, 2, 0, 4, 0, 0, 1 }, 3.0f, false },
    } },
    { "cone-light", 32, 32, {
    } },
    { "console", 16, 16, {
        { "active", { 0, 1 }, 5.0f, true },
        { "green", { 2 }, 1.0f, true },
        { "spaz", { 0, 1, 2 }, 20.0f, true },
        { "flicker", { 0, 1, 2 }, 10.0f, true },
    } },
    { "contort_big", 16, 32, {
        { "move", { 0, 1, 2, 1 }, 9.0f, true },
    } },
    { "contort_small", 16, 16, {
        { "move", { 0, 1 }, 9.0f, true },  // variant-relative
    } },
    { "crackedtiles", 16, 16, {
    } },
    { "dame-switch-pillar", 16, 16, {
        { "up", { 0 }, 1.0f, false },
        { "down", { 1 }, 1.0f, false },
        { "dissolve", { 0, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 1 }, 15.0f, false },
        { "solidify", { 1, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 0 }, 15.0f, false },
    } },
    { "dash_pads", 16, 16, {
    } },
    { "dash_trap", 16, 16, {
        { "idle", { 6 }, 1.0f, true },
        { "dash", { 6 }, 1.0f, true },
        { "bounce", { 6 }, 1.0f, true },
        { "idle@2", { 4 }, 12.0f, true },
        { "dash@2", { 5 }, 12.0f, true },
        { "bounce@2", { 4, 5 }, 12.0f, true },
    } },
    { "dev_npcs", 16, 16, {
    } },
    { "dog", 16, 16, {
        { "stop", { 0 }, 1.0f, true },
        { "walk", { 2, 3 }, 4.0f, true },
        { "alert", { 4, 5 }, 4.0f, true },
        { "attack", { 6, 7 }, 6.0f, true },
    } },
    { "dust", 16, 16, {
        { "poofed", { 0 }, 1.0f, true },
        { "unpoofed", { 4 }, 1.0f, true },
        { "poof", { 0, 1, 2, 3, 4 }, 13.0f, false },
        { "fallpoof", { 0, 1, 2, 3, 4 }, 13.0f, false },
        { "unpoof", { 3, 2, 1, 0 }, 13.0f, false },
    } },
    { "dustmaid", 16, 24, {
        { "idle", { 0 }, 1.0f, true },
        { "turn_dark", { 1, 2, 1, 2 }, 12.0f, false },
        { "move_r", { 5, 6 }, 5.0f, true },
        { "move_l", { 5, 6 }, 5.0f, true },
        { "move_d", { 3, 4 }, 5.0f, true },
        { "move_u", { 7, 8 }, 5.0f, true },
    } },
    { "elevator", 32, 32, {
    } },
    { "elevator_doors", 32, 32, {
        { "default", { 0 }, 1.0f, true },
        { "open", { 0, 1, 2, 3 }, 12.0f, false },
        { "close", { 3, 2, 1, 0 }, 12.0f, false },
    } },
    { "enemy_explode_2", 24, 24, {
        { "explode", { 0, 1, 2, 3, 4 }, 12.0f, false },  // variant-relative
        { "explode@2", { 0, 1, 2, 3, 4 }, 14.0f, false },
    } },
    { "eye_boss_bullet", 16, 16, {
        { "move", { 6, 7 }, 12.0f, true },
        { "pop", { 2, 3, 4, 5 }, 24.0f, false },
        { "move@2", { 0, 1 }, 12.0f, true },
        { "pop@2", { 2, 3, 4, 5 }, 12.0f, false },
    } },
    { "eye_boss_splash", 8, 8, {
        { "move", { 0, 1 }, 10.0f, true },
        { "pop", { 2, 3 }, 12.0f, false },
    } },
    { "eye_boss_water", 24, 24, {
        { "closed", { 3 }, 1.0f, true },
        { "walk", { 4, 5 }, 6.0f, true },
        { "blink_land", { 5, 6, 7, 6, 5 }, 6.0f, false },
    } },
    { "eyelids", 24, 24, {
        { "closed", { 3 }, 1.0f, true },
        { "blink", { 0, 1, 2, 3, 2, 1, 0 }, 10.0f, false },
        { "blink_fast", { 0, 1, 2, 3, 2, 1 }, 20.0f, true },
        { "idle", { 0 }, 1.0f, true },
        { "open", { 3, 2, 1, 0 }, 5.0f, false },
    } },
    { "eyelight", 16, 16, {
        { "glow", { 0, 1, 2 }, 5.0f, true },
    } },
    { "f_four_shooter", 16, 16, {
        { "idle", { 0 }, 1.0f, true },
        { "axis_to_diag", { 0, 1, 2 }, 3.0f, false },
        { "diag_to_axis", { 2, 1, 0 }, 3.0f, false },
    } },
    { "f_four_shooter_bullet", 8, 8, {
        { "move", { 0, 1 }, 12.0f, true },
        { "explode", { 2, 3 }, 10.0f, false },
    } },
    { "f_mover", 16, 16, {
        { "Foom", { 0, 1 }, 4.0f, false },
        { "Die", { 0, 1, 2, 1, 2, 1, 2 }, 12.0f, false },
    } },
    { "f_rotator", 16, 16, {
        { "pulse", { 0, 1 }, 10.0f, true },
    } },
    { "f_rotator_ball", 8, 8, {
        { "pulse", { 0, 1 }, 10.0f, true },
    } },
    { "f_slasher", 24, 24, {
        { "float_d", { 0, 1 }, 3.0f, true },
        { "float_l", { 2, 3 }, 3.0f, true },
        { "float_u", { 4, 5 }, 3.0f, true },
        { "warning_d", { 6, 6 }, 10.0f, false },
        { "warning_l", { 7, 7 }, 10.0f, false },
        { "warning_u", { 8, 8 }, 10.0f, false },
        { "attack_d", { 9, 9 }, 10.0f, false },
        { "attack_l", { 10, 10 }, 10.0f, false },
        { "attack_u", { 11, 11 }, 10.0f, false },
        { "die", { 9, 10, 11, 11 }, 3.0f, false },
    } },
    { "f_slasher_long", 16, 48, {
        { "wide", { 0, 1, 2 }, 12.0f, false },
        { "tall", { 3, 4, 5 }, 12.0f, false },
    } },
    { "f_slasher_wide", 48, 16, {
        { "wide", { 0, 1, 2 }, 12.0f, false },
        { "tall", { 3, 4, 5 }, 12.0f, false },
    } },
    { "f_wallboss_face", 64, 32, {
        { "normal", { 0, 2 }, 5.0f, true },
        { "hurt", { 3, 5 }, 14.0f, true },
        { "charge", { 1 }, 1.0f, true },
        { "shoot", { 4 }, 1.0f, true },
    } },
    { "f_wallboss_l_hand", 32, 32, {
        { "idle", { 0 }, 1.0f, true },
        { "stomp", { 1 }, 1.0f, true },
        { "shoot", { 2 }, 1.0f, true },
        { "push", { 3 }, 1.0f, true },
    } },
    { "fields_npcs", 16, 16, {
        { "walk_d", { 10, 11 }, 4.0f, true },
        { "walk_r", { 12, 13 }, 4.0f, true },
        { "walk_u", { 14, 15 }, 4.0f, true },
        { "walk_l", { 16, 17 }, 4.0f, true },
        { "walk_d@2", { 0, 1 }, 4.0f, true },
        { "walk_r@2", { 2, 3 }, 4.0f, true },
        { "walk_u@2", { 4, 5 }, 4.0f, true },
        { "walk_l@2", { 6, 7 }, 4.0f, true },
        { "walk_d@3", { 20, 21 }, 4.0f, true },
        { "walk_r@3", { 22, 23 }, 4.0f, true },
        { "walk_u@3", { 24, 25 }, 4.0f, true },
        { "closed", { 31 }, 1.0f, true },
        { "open", { 32 }, 1.0f, true },
        { "a", { 50, 51 }, 4.0f, true },
        { "a@2", { 0, 1 }, 4.0f, true },  // variant-relative
    } },
    { "fields_npcs", 32, 32, {
        { "a", { 15, 15, 15, 15, 15, 15, 15, 15, 16, 17, 17, 18, 18 }, 18.0f, true },
    } },
    { "fire_pillar", 16, 32, {
        { "idle", { 0 }, 1.0f, true },
        { "emerge", { 1, 2, 3, 4 }, 8.0f, false },
        { "flame", { 3, 4 }, 10.0f, true },
        { "recede", { 5, 6, 0 }, 8.0f, false },
    } },
    { "fire_pillar_base", 16, 16, {
        { "dormant", { 0, 1 }, 6.0f, true },
    } },
    { "follower_bro", 16, 24, {
        { "wait", { 0 }, 1.0f, true },
        { "walk", { 1, 2, 1, 0 }, 4.0f, true },
    } },
    { "forest_npcs", 16, 16, {
        { "idle", { 30 }, 1.0f, true },
        { "hop", { 32, 33 }, 4.0f, false },
        { "walk_d", { 30, 31 }, 4.0f, true },
        { "walk_r", { 32, 33 }, 4.0f, true },
        { "walk_u", { 34, 35 }, 4.0f, true },
        { "walk_l", { 36, 37 }, 4.0f, true },
        { "idle@2", { 20 }, 1.0f, true },
        { "move", { 20, 21, 20, 22, 20, 21, 20, 22, 20 }, 8.0f, false },
        { "move@2", { 0, 1 }, 4.0f, true },
        { "stand", { 1 }, 1.0f, true },
        { "squat", { 0 }, 1.0f, true },
        { "a", { 10, 10, 10, 11, 10, 10, 10, 12 }, 3.0f, true },
    } },
    { "forest_stag", 64, 80, {
        { "a", { 0, 1, 2 }, 4.0f, true },
    } },
    { "frog_bullet", 8, 8, {
        { "move", { 4, 5 }, 12.0f, true },
        { "explode", { 2, 3, 3 }, 10.0f, false },
        { "move@2", { 0, 1 }, 12.0f, true },
    } },
    { "gas_guy", 16, 24, {
        { "float", { 0, 1 }, 2.0f, true },
        { "release_gas", { 2 }, 20.0f, true },
    } },
    { "gas_guy_cloud", 24, 24, {
        { "move", { 0, 1 }, 3.0f, true },
    } },
    { "gate_green", 32, 16, {
        { "0", { 0 }, 1.0f, true },
        { "1", { 1 }, 1.0f, true },
        { "2", { 2 }, 1.0f, true },
        { "3", { 3 }, 1.0f, true },
        { "4", { 4 }, 1.0f, true },
    } },
    { "gate_green_digits", 3, 5, {
    } },
    { "gate_green_slots", 32, 16, {
        { "key", { 3 }, 1.0f, true },
        { "a", { 0 }, 1.0f, true },  // variant-relative
    } },
    { "gates", 16, 16, {
        { "still", { 0 }, 1.0f, true },  // variant-relative
        { "opened", { 3 }, 1.0f, true },  // variant-relative
        { "close", { 3, 2, 1, 0 }, 8.0f, false },  // variant-relative
        { "open", { 0, 1, 2, 3 }, 4.0f, false },  // variant-relative
    } },
    { "glow-light", 64, 64, {
    } },
    { "happy_npcs", 16, 16, {
        { "idle", { 10 }, 1.0f, true },  // variant-relative
        { "fall", { 10, 11, 12, 13, 14 }, 7.0f, false },  // variant-relative
        { "idle_d", { 0 }, 4.0f, true },  // variant-relative
        { "idle_r", { 2 }, 4.0f, true },  // variant-relative
        { "idle_u", { 4 }, 4.0f, true },  // variant-relative
        { "idle_l", { 6 }, 4.0f, true },  // variant-relative
        { "walk_d", { 0, 1 }, 4.0f, true },  // variant-relative
        { "walk_r", { 2, 3 }, 4.0f, true },  // variant-relative
        { "walk_u", { 4, 5 }, 4.0f, true },  // variant-relative
        { "walk_l", { 6, 7 }, 4.0f, true },  // variant-relative
    } },
    { "health_piece", 11, 6, {
        { "full", { 0 }, 1.0f, true },
        { "empty", { 1 }, 1.0f, true },
        { "flash", { 0, 2 }, 7.0f, true },
    } },
    { "hole", 16, 16, {
    } },
    { "hotel_npcs", 16, 16, {
        { "idle_d", { 0 }, 1.0f, true },
        { "idle_r", { 1 }, 1.0f, true },
        { "idle_u", { 2 }, 1.0f, true },
        { "idle_l", { 3 }, 1.0f, true },
    } },
    { "item_jump_shoes", 16, 16, {
    } },
    { "javiera", 16, 16, {
        { "walk_d", { 0, 1 }, 8.0f, true },
        { "walk_l", { 4, 5 }, 8.0f, true },
        { "walk_u", { 2, 3 }, 8.0f, true },
        { "walk_r", { 4, 5 }, 8.0f, true },
        { "juggle", { 0, 1 }, 8.0f, true },
        { "fall", { 6, 7, 8, 9, 10, 11, 12 }, 2.0f, false },
        { "pre-fall", { 1 }, 1.0f, true },
    } },
    { "javiera_juggle", 16, 24, {
        { "juggle", { 0, 1 }, 8.0f, true },
        { "walk_d", { 0, 1 }, 8.0f, true },
        { "walk_l", { 4, 5 }, 8.0f, true },
        { "walk_u", { 2, 3 }, 8.0f, true },
        { "walk_r", { 4, 5 }, 8.0f, true },
        { "fall", { 6, 7, 8, 9, 10, 11, 12 }, 2.0f, false },
    } },
    { "key", 16, 16, {
    } },
    { "key_green", 16, 16, {
    } },
    { "key_sparkle", 7, 7, {
        { "sparkle", { 3, 2, 1, 0 }, 8.0f, false },
    } },
    { "keyhole", 16, 16, {
        { "closed", { 0 }, 1.0f, true },
        { "Open", { 16, 17, 18, 19, 20 }, 10.0f, false },
    } },
    { "knife", 16, 16, {
        { "stab", { 1, 2, 2, 1, 0, 0 }, 20.0f, false },
    } },
    { "life_cicada", 16, 16, {
        { "idle", { 2, 3, 2, 3, 2, 2, 2, 2 }, 8.0f, true },  // variant-relative
        { "fly", { 0, 1 }, 14.0f, true },  // variant-relative
        { "gnaw", { 2, 3 }, 14.0f, true },  // variant-relative
    } },
    { "life_cicada_particle", 4, 4, {
    } },
    { "light_orb", 16, 16, {
        { "glow", { 0, 1, 2, 3, 0 }, 10.0f, true },  // variant-relative
    } },
    { "lion", 32, 32, {
        { "walk_r", { 0, 1 }, 5.0f, true },
        { "walk_l", { 0, 1 }, 5.0f, true },
        { "walk_d", { 10, 11 }, 5.0f, true },
        { "walk_u", { 5, 6 }, 5.0f, true },
        { "warn_l", { 3 }, 5.0f, true },
        { "warn_r", { 3 }, 5.0f, true },
        { "shoot_l", { 2 }, 15.0f, true },
        { "shoot_r", { 2 }, 15.0f, true },
        { "shoot_d", { 12 }, 15.0f, true },
        { "shoot_u", { 7 }, 15.0f, true },
        { "pounce_r", { 4 }, 1.0f, true },
        { "pounce_l", { 4 }, 1.0f, true },
        { "pounce_u", { 9 }, 1.0f, true },
        { "pounce_d", { 14 }, 1.0f, true },
    } },
    { "lion_fireballs", 16, 16, {
        { "shoot", { 0, 1 }, 8.0f, true },
        { "poof", { 2, 3, 4, 5 }, 8.0f, false },
    } },
    { "long_attack_h", 22, 12, {
        { "a", { 0, 1, 2, 3, 4 }, 14.0f, false },
    } },
    { "menu_select_active", 61, 15, {
        { "flash", { 0, 1 }, 4.0f, true },
        { "inactive", { 2 }, 1.0f, true },
    } },
    { "mini_minimap_tiles", 5, 5, {
    } },
    { "minimap_tiles", 7, 7, {
    } },
    { "mitra", 16, 16, {
        { "walk_d", { 0, 1 }, 8.0f, true },
        { "walk_l", { 2, 3 }, 8.0f, true },
        { "walk_r", { 2, 3 }, 8.0f, true },
        { "walk_u", { 4, 5 }, 8.0f, true },
        { "idle_d", { 6 }, 8.0f, true },
        { "idle_l", { 7 }, 8.0f, true },
        { "idle_r", { 7 }, 8.0f, true },
        { "idle_u", { 8 }, 8.0f, true },
    } },
    { "mitra_bike", 20, 20, {
        { "bike_r", { 2, 3 }, 8.0f, true },
        { "bike_l", { 2, 3 }, 8.0f, true },
        { "idle", { 22 }, 1.0f, true },
        // Mitra.OnBike swaps the texture under the walking sprite's anims (Mitra.GetSprite).
        { "walk_d", { 0, 1 }, 8.0f, true },
        { "walk_l", { 2, 3 }, 8.0f, true },
        { "walk_r", { 2, 3 }, 8.0f, true },
        { "walk_u", { 4, 5 }, 8.0f, true },
        { "idle_d", { 6 }, 8.0f, true },
        { "idle_l", { 7 }, 8.0f, true },
        { "idle_r", { 7 }, 8.0f, true },
        { "idle_u", { 8 }, 8.0f, true },
    } },
    { "moving_platform", 16, 16, {
        { "unpressed-inactive", { 0 }, 1.0f, true },  // variant-relative
        { "unpressed-active", { 1 }, 1.0f, true },  // variant-relative
        { "pressed-inactive", { 2 }, 1.0f, true },  // variant-relative
        { "pressed-active", { 3 }, 1.0f, true },  // variant-relative
    } },
    { "moving_platform_poof", 16, 16, {
        { "play", { 0, 1, 2, 3, 4 }, 12.0f, false },
    } },
    { "nexus_door_preview_overlay", 32, 32, {
        { "stop", { 0 }, 1.0f, true },  // variant-relative
        { "preview", { 0, 1, 2, 3 }, 10.0f, true },  // variant-relative
    } },
    { "nexus_pad", 32, 32, {
        { "off", { 0 }, 1.0f, true },  // variant-relative
        { "on", { 1 }, 1.0f, true },  // variant-relative
    } },
    { "note_rock", 16, 16, {
    } },
    { "npc", 16, 16, {
    } },
    { "on_off_shooter", 16, 16, {
        { "shooter_closed", { 0, 1 }, 2.0f, true },
        { "shooter_open", { 2 }, 1.0f, true },
        { "shooter_closed_b", { 3, 4 }, 2.0f, true },
        { "shooter_open_b", { 5 }, 1.0f, true },
    } },
    { "overlay_APARTMENT_water", 24, 24, {
    } },
    { "overlay_CIRCUS_water", 24, 24, {
    } },
    { "overlay_DEBUG_water", 24, 24, {
    } },
    { "overlay_FIELDS_grass", 24, 24, {
    } },
    { "overlay_FOREST_water", 24, 24, {
    } },
    { "overlay_REDCAVE_water", 24, 24, {
    } },
    { "overlay_REDSEA_water", 24, 24, {
    } },
    { "overlay_water", 24, 24, {
        { "water", { 0, 1 }, 5.0f, true },
        { "grass_go", { 4, 5 }, 8.0f, true },
        { "grass_stop", { 5 }, 1.0f, true },
    } },
    { "person", 16, 16, {
        { "walk_d", { 0, 1 }, 5.0f, true },
        { "walk_r", { 2, 3 }, 5.0f, true },
        { "walk_u", { 4, 5 }, 5.0f, true },
        { "walk_l", { 2, 3 }, 5.0f, true },
    } },
    { "pew_laser", 16, 16, {
    } },
    { "pew_laser_bullet", 16, 8, {
        { "shoot", { 0, 1 }, 8.0f, true },
        { "poof", { 4, 5, 6, 7 }, 8.0f, false },
    } },
    { "pillar_switch", 16, 16, {
        { "up", { 0 }, 1.0f, true },
        { "down", { 1 }, 1.0f, true },
        { "hit", { 2, 3 }, 12.0f, true },
    } },
    { "player-light", 64, 64, {
    } },
    { "pupil", 10, 8, {
    } },
    { "rail", 16, 16, {
    } },
    { "rail_CROWD", 16, 22, {
    } },
    { "randoms", 16, 16, {
    } },
    { "rat", 16, 16, {
        { "walk_d", { 0, 1 }, 5.0f, true },  // variant-relative
        { "walk_l", { 2, 3 }, 5.0f, true },  // variant-relative
        { "walk_r", { 2, 3 }, 5.0f, true },  // variant-relative
        { "walk_u", { 4, 5 }, 5.0f, true },  // variant-relative
    } },
    { "red_boss", 32, 32, {
        { "close_eyes", { 1 }, 1.0f, true },
        { "bob", { 0 }, 1.0f, true },
        { "warn", { 2 }, 1.0f, true },
        { "die", { 0, 1, 2, 1 }, 3.0f, false },
    } },
    { "red_boss_big_wave", 32, 80, {
        { "move", { 0, 1 }, 8.0f, true },
        { "rise", { 2, 1, 0 }, 8.0f, false },
        { "fall", { 1, 2, 3 }, 8.0f, false },
    } },
    { "red_boss_bullet", 8, 8, {
        { "move", { 0, 1 }, 12.0f, true },
        { "explode", { 2, 3, 4 }, 14.0f, false },
    } },
    { "red_boss_ripple", 48, 8, {
        { "a", { 0, 1 }, 12.0f, true },
    } },
    { "red_boss_small_wave", 16, 64, {
        { "move", { 0, 1 }, 8.0f, true },
        { "rise", { 2, 3 }, 8.0f, true },
        { "fall", { 1, 2, 3, 4 }, 8.0f, false },
    } },
    { "red_boss_tentacle", 10, 64, {
        { "move", { 0, 1 }, 8.0f, true },
    } },
    { "red_boss_warning", 10, 10, {
        { "move", { 0, 1 }, 8.0f, true },
    } },
    { "red_cave_left", 64, 64, {
    } },
    { "red_pillar", 16, 80, {
        { "idle", { 0 }, 1.0f, true },
        { "move", { 1, 2 }, 8.0f, true },
    } },
    { "red_pillar_ripple", 16, 16, {
        { "move", { 0, 1 }, 8.0f, true },
    } },
    { "redsea_npcs", 16, 16, {
        { "walk", { 10, 11 }, 4.0f, true },
        { "walk_d", { 0, 1 }, 4.0f, true },
        { "walk_r", { 2, 3 }, 4.0f, true },
        { "walk_u", { 4, 5 }, 4.0f, true },
        { "walk_l", { 6, 7 }, 4.0f, true },
    } },
    { "redwalker", 32, 48, {
        { "walk", { 0, 1, 2, 3, 4 }, 5.0f, true }, // (RedWalker starts it on a random frame)
    } },
    { "sadman", 16, 16, {
        { "a", { 0, 1 }, 2.0f, true },
    } },
    { "sage", 16, 16, {
        { "idle_d", { 6 }, 1.0f, true },
        { "idle_r", { 7 }, 1.0f, true },
        { "idle_l", { 7 }, 1.0f, true },
        { "idle_u", { 8 }, 1.0f, true },
        { "walk_d", { 0, 1 }, 6.0f, true },
        { "walk_r", { 2, 3 }, 6.0f, true },
        { "walk_l", { 2, 3 }, 6.0f, true },
        { "walk_u", { 4, 5 }, 6.0f, true },
    } },
    { "sage_attacks", 16, 16, {
        { "spin", { 0, 1 }, 24.0f, true },
        { "poof", { 0, 1, 2 }, 12.0f, false },
        { "shoot", { 4, 5 }, 12.0f, true },
        { "a", { 0, 1 }, 8.0f, true },
    } },
    { "sage_boss", 16, 24, {
        { "idle", { 4 }, 1.0f, true },
        { "idle_d", { 0, 1, 2, 3 }, 5.0f, true },
        { "dash_d", { 0, 1, 2, 3 }, 5.0f, true },
        { "dash_u", { 0, 1, 2, 3 }, 5.0f, true },
        { "walk_u", { 0, 1, 2, 3 }, 5.0f, true },
    } },
    { "sage_fight_long_dust", 64, 16, {
        { "spin", { 0, 1 }, 24.0f, true },
        { "poof", { 0, 1, 2 }, 12.0f, false },
    } },
    { "sage_statue", 16, 16, {
    } },
    { "sclera", 24, 24, {
    } },
    { "secret_trophies", 16, 16, {
    } },
    { "selector", 16, 16, {
        { "a", { 0, 1 }, 4.0f, true },
    } },
    { "shieldy", 16, 16, {
        { "walk", { 1, 2, 1, 0 }, 5.0f, true },
        { "front_hit", { 16, 17, 18, 1 }, 12.0f, false },
        { "back_hit", { 13, 1 }, 12.0f, true },
    } },
    { "shockwave", 16, 16, {
        { "move", { 0, 1, 2, 1 }, 8.0f, true },
        { "evaporate", { 3, 4, 5, 6, 6 }, 8.0f, false },
    } },
    { "silverfish", 16, 16, {
        { "move_l", { 6, 7 }, 7.0f, true },  // variant-relative
        { "move_d", { 4, 5 }, 7.0f, true },  // variant-relative
        { "move_r", { 6, 7 }, 7.0f, true },  // variant-relative
        { "move_u", { 8, 9 }, 7.0f, true },  // variant-relative
        { "idle_r", { 6 }, 12.0f, true },  // variant-relative
        { "idle_d", { 4 }, 12.0f, true },  // variant-relative
        { "idle_u", { 8 }, 12.0f, true },  // variant-relative
        { "idle_l", { 6 }, 12.0f, true },  // variant-relative
    } },
    { "slime", 16, 16, {
        { "Move", { 0, 1 }, 3.0f, true },  // variant-relative
        { "Hurt", { 0, 6, 0, 6 }, 15.0f, false },  // variant-relative
        { "Dying", { 0, 6, 0, 6 }, 12.0f, false },  // variant-relative
        { "Move@cell", { 4, 5 }, 3.0f, true },
        { "Hurt@cell", { 4, 6, 4, 6 }, 15.0f, false },
        { "Dying@cell", { 4, 6, 4, 6 }, 12.0f, false },
    } },
    // SolidColorRenderer squares (made by the content compiler): black and yellow 20x20, a red dot, and black, red,
    // green and blue 32x32.
    { "solid_square", 20, 20, {
    } },
    { "solid_dot", 2, 2, {
    } },
    { "solid_32", 32, 32, {
    } },
    { "slime_bullet", 8, 8, {
        { "move", { 0, 1 }, 10.0f, true },
    } },
    { "slime_goo", 6, 6, {
        { "move", { 0, 1, 2, 3, 1, 3, 1, 2, 1, 0 }, 8.0f, true },  // variant-relative
        { "splash", { 0 }, 1.0f, true },  // variant-relative
    } },
    { "small_health_pickup", 10, 16, {
        { "float", { 0, 1, 2, 3 }, 5.0f, true },  // variant-relative
    } },
    { "smoke_red", 32, 32, {
    } },
    { "space_npcs", 16, 16, {
        { "a", { 20, 21 }, 6.0f, true },
        { "a@2", { 22, 23 }, 6.0f, true },
        { "a@3", { 0, 10, 1, 11 }, 10.0f, true },
        { "walk_d", { 0, 1 }, 4.0f, true },  // variant-relative
        { "walk_r", { 2, 3 }, 4.0f, true },  // variant-relative
        { "walk_u", { 4, 5 }, 4.0f, true },  // variant-relative
        { "walk_l", { 6, 7 }, 4.0f, true },  // variant-relative
        { "spin", { 0, 2, 4, 6 }, 10.0f, true },  // variant-relative
        { "dead", { 8 }, 1.0f, true },  // variant-relative
    } },
    { "space_npcs", 32, 32, {
        { "a", { 0, 1 }, 4.0f, true },  // variant-relative
    } },
    { "spike_roller_horizontal_shadow", 128, 16, {
    } },
    { "spike_roller_shadow", 16, 128, {
    } },
    { "splitboss", 24, 32, {
        { "float", { 0, 1, 2, 1 }, 5.0f, true },
        { "idle_r", { 4, 5, 6, 5 }, 5.0f, true },
        { "dash_r", { 10, 11 }, 8.0f, true },
        { "dash_d", { 8, 9 }, 8.0f, true },
        { "die", { 0, 3 }, 14.0f, true },
    } },
    { "splitboss_fireball", 16, 16, {
        { "pulsate", { 0, 1, 2, 3 }, 12.0f, true },
        { "fizzle", { 4, 5, 6, 7 }, 12.0f, false },
    } },
    { "spring_pad", 16, 16, {
        { "still", { 0 }, 1.0f, true },
        { "pressed", { 1 }, 1.0f, true },
        { "wobble", { 0, 2, 0, 1, 0, 2, 0, 1, 0, 0 }, 10.0f, false },
    } },
    { "steam", 16, 16, {
        { "s", { 0, 1 }, 10.0f, true },
    } },
    { "steam_pipe", 16, 16, {
    } },
    { "suburb_killers", 16, 16, {
        { "idle_d", { 0 }, 1.0f, true },  // variant-relative
        { "idle_r", { 2 }, 1.0f, true },  // variant-relative
        { "idle_u", { 4 }, 1.0f, true },  // variant-relative
        { "idle_l", { 6 }, 1.0f, true },  // variant-relative
        { "walk_d", { 0, 1 }, 4.0f, true },  // variant-relative
        { "walk_r", { 2, 3 }, 4.0f, true },  // variant-relative
        { "walk_u", { 4, 5 }, 4.0f, true },  // variant-relative
        { "walk_l", { 6, 7 }, 4.0f, true },  // variant-relative
    } },
    { "suburb_walkers", 16, 16, {
        { "a", { 0, 1 }, 4.0f, true },
        { "walk_d", { 0, 1 }, 4.0f, true },  // variant-relative
        { "walk_r", { 2, 3 }, 4.0f, true },  // variant-relative
        { "walk_u", { 4, 5 }, 4.0f, true },  // variant-relative
        { "walk_l", { 6, 7 }, 4.0f, true },  // variant-relative
        { "hanged", { 54 }, 1.0f, true },
        { "a@2", { 55 }, 1.0f, true },
        { "die", { 0, 2, 4, 6, 0, 2, 4, 6, 8 }, 8.0f, false },  // variant-relative
        { "dead", { 8 }, 1.0f, true },  // variant-relative
        { "a@3", { 63, 64, 65, 66 }, 3.0f, false },
    } },
    { "sun_guy", 16, 24, {
        { "float", { 0, 1, 2, 3, 4 }, 3.0f, true },
    } },
    { "sun_guy_wave", 128, 8, {
        { "play", { 3, 4, 5 }, 8.0f, true },
        { "evaporate", { 2, 1, 0 }, 8.0f, false },
    } },
    { "teeny_shadow", 3, 3, {
    } },
    { "teleport_guy", 16, 24, {
        { "idle_d", { 0, 1 }, 3.0f, true },
        { "idle_r", { 2, 3 }, 3.0f, true },
        { "idle_u", { 4, 5 }, 3.0f, true },
        { "idle_l", { 2, 3 }, 3.0f, true },
        { "poof", { 6, 7, 8, 9 }, 12.0f, false },
        { "unpoof", { 8, 7, 6 }, 12.0f, false },
        { "dying", { 0 }, 1.0f, true },
    } },
    { "treasureboxes", 16, 16, {
    } },
    { "volume_bar", 60, 12, {
    } },
    { "wallboss_bullet", 8, 8, {
        { "move", { 0, 1 }, 12.0f, true },
        { "explode", { 2, 3, 4 }, 10.0f, false },
    } },
    { "wallboss_laser", 64, 10, {
        { "charge", { 0 }, 1.0f, true },
        { "attack", { 1, 2 }, 12.0f, true },
    } },
    { "wallboss_wall", 160, 32, {
        { "move", { 0, 1 }, 4.0f, true },
    } },
    { "whirlpool", 16, 16, {
        { "whirl", { 0, 1 }, 6.0f, true },
        { "transition", { 3, 4, 4 }, 6.0f, false },
        { "whirl_red", { 4, 5 }, 6.0f, true },
    } },
    { "whiteportal", 16, 16, {
        { "a", { 0, 1, 2 }, 8.0f, true },
        { "a@cell", { 4, 5 }, 8.0f, true },
    } },
    { "wide_attack_v", 12, 24, {
        { "a", { 0, 1, 2, 3, 4 }, 14.0f, false },
    } },
    // Shell.Blade: one idle/rotate pair per blade (its start frame); rotate runs on 32 frames, 24 to a quarter turn.
    // Frames 24-47 are the compiler's quarter-turned copies.
    { "windmill_blade", 192, 192, {
        { "idle", { 0 }, 1.0f, true },
        { "rotate", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 0, 1, 2, 3, 4, 5, 6, 7 }, 20.0f, true },
        { "idle@8", { 8 }, 1.0f, true },
        { "rotate@8", { 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, 20.0f, true },
        { "idle@16", { 16 }, 1.0f, true },
        { "rotate@16", { 16, 17, 18, 19, 20, 21, 22, 23, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 }, 20.0f, true },
    } },
    { "windmill_inside", 48, 48, {
        { "idle", { 1 }, 1.0f, true },
        { "active", { 0, 1 }, 5.0f, true },
    } },
    { "windmill_shell", 48, 48, {
    } },
    { "young_player", 16, 16, {
        { "walk_d", { 1, 0 }, 6.0f, true },
        { "walk_r", { 2, 3 }, 8.0f, true },
        { "walk_u", { 4, 5 }, 6.0f, true },
        { "walk_l", { 6, 7 }, 8.0f, true },
        { "attack_d", { 8, 9 }, 10.0f, false },
        { "attack_r", { 10, 11 }, 10.0f, false },
        { "attack_u", { 12, 13 }, 10.0f, false },
        { "attack_l", { 14, 15 }, 10.0f, false },
        { "fall", { 28, 29, 30, 31 }, 5.0f, false },
        { "slumped", { 32 }, 1.0f, true },
        { "whirl", { 25, 26, 27, 24 }, 12.0f, true },
        { "idle_d", { 24 }, 4.0f, true },
        { "idle_r", { 25 }, 4.0f, true },
        { "idle_u", { 26 }, 4.0f, true },
        { "idle_l", { 27 }, 4.0f, true },
        { "jump_d", { 16, 17 }, 4.0f, true },
        { "jump_r", { 18, 19 }, 4.0f, true },
        { "jump_u", { 20, 21 }, 4.0f, true },
        { "jump_l", { 22, 23 }, 4.0f, true },
        { "enter_fall", { 2 }, 1.0f, true },
        { "idle_climb", { 33 }, 1.0f, true },
        { "climb", { 34, 35 }, 8.0f, true },
        { "die", { 25, 26, 27, 24, 25, 26, 27, 24, 25, 26, 27, 32 }, 12.0f, false },
    } },
    { "young_player_cell", 16, 16, {
        { "walk_d", { 1, 0 }, 6.0f, true },
        { "walk_r", { 2, 3 }, 8.0f, true },
        { "walk_u", { 4, 5 }, 6.0f, true },
        { "walk_l", { 6, 7 }, 8.0f, true },
        { "attack_d", { 8, 9 }, 10.0f, false },
        { "attack_r", { 10, 11 }, 10.0f, false },
        { "attack_u", { 12, 13 }, 10.0f, false },
        { "attack_l", { 14, 15 }, 10.0f, false },
        { "fall", { 28, 29, 30, 31 }, 5.0f, false },
        { "slumped", { 32 }, 1.0f, true },
        { "whirl", { 25, 26, 27, 24 }, 12.0f, true },
        { "idle_d", { 24 }, 4.0f, true },
        { "idle_r", { 25 }, 4.0f, true },
        { "idle_u", { 26 }, 4.0f, true },
        { "idle_l", { 27 }, 4.0f, true },
        { "jump_d", { 16, 17 }, 4.0f, true },
        { "jump_r", { 18, 19 }, 4.0f, true },
        { "jump_u", { 20, 21 }, 4.0f, true },
        { "jump_l", { 22, 23 }, 4.0f, true },
        { "enter_fall", { 2 }, 1.0f, true },
        { "idle_climb", { 33 }, 1.0f, true },
        { "climb", { 34, 35 }, 8.0f, true },
        { "die", { 25, 26, 27, 24, 25, 26, 27, 24, 25, 26, 27, 32 }, 12.0f, false },
    } },
    { "young_player_reflection", 16, 16, {
    } },
    // --- Added by hand: call sites the scraper couldn't read (ternary frame sizes, anims built in a variable,
    // sheets only referenced through UI/treasure constructors).
    { "f_on_off_h", 144, 16, {
        { "idle", { 0 }, 1.0f, true },
        { "steam_is_harmful", { 3, 4, 5, 6 }, 15.0f, true },
        { "recede", { 0, 1, 2 }, 5.0f, false },
        { "emerge", { 2, 1, 0 }, 5.0f, false },
    } },
    { "f_on_off_v", 16, 144, {
        { "idle", { 0 }, 1.0f, true },
        { "steam_is_harmful", { 3, 4, 5, 6 }, 15.0f, true },
        { "recede", { 0, 1, 2 }, 5.0f, false },
        { "emerge", { 2, 1, 0 }, 5.0f, false },
    } },
    { "spike_roller", 16, 128, {
        { "idle", { 0 }, 1.0f, true },
        { "roll", { 0, 1 }, 5.0f, true },
    } },
    { "spike_roller_horizontal", 128, 16, {
        { "idle", { 0 }, 1.0f, true },
        { "roll", { 0, 1 }, 5.0f, true },
    } },
    { "frog", 16, 16, {
        { "idle", { 0, 1 }, 2.0f, true },
        { "shoot_d", { 3 }, 3.0f, false },
        { "shoot_r", { 4 }, 3.0f, false },
        { "shoot_l", { 4 }, 3.0f, false },
        { "shoot_u", { 5 }, 3.0f, false },
        { "idle@2", { 6, 7 }, 2.0f, true }, // BoiEaster variant
        { "shoot_d@2", { 8 }, 3.0f, false },
        { "shoot_r@2", { 8 }, 3.0f, false },
        { "shoot_l@2", { 8 }, 3.0f, false },
        { "shoot_u@2", { 8 }, 3.0f, false },
    } },
    { "PixieCatSupreme", 16, 16, {
    } },
    { "broom-icon", 16, 16, {
    } },
    { "item_long_attack", 16, 16, {
    } },
    { "item_tranformer", 16, 16, {
    } },
    { "item_wide_attack", 16, 16, {
    } },
    { "card_sheet", 24, 24, {
    } },
    { "card_49", 24, 24, {
    } },
    { "nexus_cardgem", 32, 16, {
    } },
    { "title_text", 128, 48, {
    } },
    { "door_spinglow1", 64, 64, {
    } },
    { "door_spinglow2", 64, 64, {
    } },
    { "door_spinglow1_inner", 64, 64, {
    } },
    { "door_spinglow2_inner", 64, 64, {
    } },
    { "dialogue_box", 156, 44, {
    } },
    { "dialogue_blinky_box", 8, 8, {
    } },
    { "title_remake", 71, 11, {
    } },
    { "press_enter", 96, 16, {
    } },
    { "door_glow", 64, 32, {
    } },
    // Generated by the compiler (see sprite_compiler.cpp): a magenta box for entities without a sprite yet.
    { "placeholder", 16, 16, {
    } },
    };
    return kSpriteSheets;
}
