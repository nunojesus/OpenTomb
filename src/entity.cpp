#include <stdlib.h>
#include <math.h>

#include "vmath.h"
#include "mesh.h"
#include "entity.h"
#include "render.h"
#include "camera.h"
#include "world.h"
#include "engine.h"
#include "console.h"
#include "script.h"
#include "gui.h"
#include "anim_state_control.h"
#include "character_controller.h"
#include "obb.h"
#include "gameflow.h"
#include "string.h"

#include "bullet/btBulletCollisionCommon.h"
#include "bullet/btBulletDynamicsCommon.h"
#include "bullet/BulletCollision/CollisionDispatch/btCollisionObject.h"


entity_p Entity_Create()
{
    entity_p ret = (entity_p)calloc(1, sizeof(entity_t));

    ret->move_type = MOVE_ON_FLOOR;
    Mat4_E(ret->transform);
    ret->state_flags = ENTITY_STATE_ENABLED | ENTITY_STATE_ACTIVE | ENTITY_STATE_VISIBLE;
    ret->type_flags = ENTITY_TYPE_DECORATION;
    ret->callback_flags = 0x00000000;               // no callbacks by default
    
    ret->OCB = 0;
    ret->sector_status = 0;
    ret->locked = 0;

    ret->self = (engine_container_p)malloc(sizeof(engine_container_t));
    ret->self->next = NULL;
    ret->self->object = ret;
    ret->self->object_type = OBJECT_ENTITY;
    ret->self->room = NULL;
    ret->self->collide_flag = 0;
    ret->obb = OBB_Create();
    ret->obb->transform = ret->transform;
    ret->bt_body = NULL;
    ret->character = NULL;
    ret->smooth_anim = 1;
    ret->current_sector = NULL;
    ret->onFrame = NULL;
    ret->bf.model = NULL;
    ret->bf.frame_time = 0.0;
    ret->bf.next_state = 0;
    ret->bf.lerp = 0.0;
    ret->bf.current_animation = 0;
    ret->bf.current_frame = 0;
    ret->bf.next_animation = 0;
    ret->bf.next_frame = 0;
    ret->bf.next = NULL;
    ret->bf.bone_tag_count = 0;
    ret->bf.bone_tags = 0;
    vec3_set_zero(ret->bf.bb_max);
    vec3_set_zero(ret->bf.bb_min);
    vec3_set_zero(ret->bf.centre);
    vec3_set_zero(ret->bf.pos);
    vec4_set_zero(ret->speed.m_floats);

    ret->activation_offset[0] = 0.0;
    ret->activation_offset[1] = 256.0;
    ret->activation_offset[2] = 0.0;
    ret->activation_offset[3] = 128.0;

    return ret;
}


void Entity_Clear(entity_p entity)
{
    if(entity)
    {
        if((entity->self->room != NULL) && (entity != engine_world.Character))
        {
            Room_RemoveEntity(entity->self->room, entity);
        }

        if(entity->obb)
        {
            OBB_Clear(entity->obb);
            free(entity->obb);
            entity->obb = NULL;
        }

        if(entity->bf.model && entity->bt_body)
        {
            for(int i=0;i<entity->bf.model->mesh_count;i++)
            {
                btRigidBody *body = entity->bt_body[i];
                if(body)
                {
                    body->setUserPointer(NULL);
                    if(body->getMotionState())
                    {
                        delete body->getMotionState();
                        body->setMotionState(NULL);
                    }
                    if(body->getCollisionShape())
                    {
                        delete body->getCollisionShape();
                        body->setCollisionShape(NULL);
                    }

                    if(body->isInWorld())
                    {
                        bt_engine_dynamicsWorld->removeRigidBody(body);
                    }
                    delete body;
                    entity->bt_body[i] = NULL;
                }
            }
        }

        if(entity->character)
        {
            Character_Clean(entity);
        }

        if(entity->self)
        {
            free(entity->self);
            entity->self = NULL;
        }

        if(entity->bf.bone_tag_count)
        {
            free(entity->bf.bone_tags);
            entity->bf.bone_tags = NULL;
            entity->bf.bone_tag_count = 0;
        }

        for(ss_bone_frame_p bf=entity->bf.next;bf!=NULL;)
        {
            ss_bone_frame_p bf_next = bf->next;
            bf->next = NULL;

            if(bf->bone_tag_count)
            {
                free(bf->bone_tags);
                bf->bone_tags = NULL;
                bf->bone_tag_count = 0;
            }

            free(bf);
            bf = bf_next;
        }
        entity->bf.next = NULL;
    }
}


void Entity_Enable(entity_p ent)
{
    if(!(ent->state_flags & ENTITY_STATE_ENABLED))
    {
        if(ent->bt_body != NULL)
        {
            for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
            {
                btRigidBody *b = ent->bt_body[i];
                if((b != NULL) && !b->isInWorld())
                {
                    bt_engine_dynamicsWorld->addRigidBody(b);
                }
            }
        }
        ent->state_flags |= ENTITY_STATE_ENABLED | ENTITY_STATE_ACTIVE | ENTITY_STATE_VISIBLE;
    }
}


void Entity_Disable(entity_p ent)
{
    if(ent->state_flags & ENTITY_STATE_ENABLED)
    {
        if(ent->bt_body != NULL)
        {
            for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
            {
                btRigidBody *b = ent->bt_body[i];
                if((b != NULL) && b->isInWorld())
                {
                    bt_engine_dynamicsWorld->removeRigidBody(b);
                }
            }
        }
        ent->state_flags = 0x0000;
    }
}

/**
 * This function enables collision for entity_p in all cases exept NULL models.
 * If collision models does not exists, function will create them;
 * @param ent - pointer to the entity.
 */
void Entity_EnableCollision(entity_p ent)
{
    if(ent->bt_body != NULL)
    {
        ent->self->collide_flag = 0x01;
        for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
        {
            btRigidBody *b = ent->bt_body[i];
            if((b != NULL) && !b->isInWorld())
            {
                bt_engine_dynamicsWorld->addRigidBody(b);
            }
        }
    }
    else
    {
        ent->self->collide_flag = 0x01;
        BT_GenEntityRigidBody(ent);
    }
}


void Entity_DisableCollision(entity_p ent)
{
    if(ent->bt_body != NULL)
    {
        ent->self->collide_flag = 0x00;
        for(uint16_t i=0;i<ent->bf.bone_tag_count;i++)
        {
            btRigidBody *b = ent->bt_body[i];
            if((b != NULL) && b->isInWorld())
            {
                bt_engine_dynamicsWorld->removeRigidBody(b);
            }
        }
    }
}


void BT_GenEntityRigidBody(entity_p ent)
{
    btScalar tr[16];
    btVector3 localInertia(0, 0, 0);
    btTransform startTransform;
    btCollisionShape *cshape;
    if(ent->bf.model == NULL)
    {
        return;
    }

    ent->bt_body = (btRigidBody**)malloc(ent->bf.model->mesh_count * sizeof(btRigidBody*));

    for(uint16_t i=0;i<ent->bf.model->mesh_count;i++)
    {
        ent->bt_body[i] = NULL;
        cshape = BT_CSfromMesh(ent->bf.model->mesh_tree[i].mesh_base, true, true, ent->self->collide_flag);
        if(cshape)
        {
            Mat4_Mat4_mul_macro(tr, ent->transform, ent->bf.bone_tags[i].full_transform);
            startTransform.setFromOpenGLMatrix(tr);
            btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
            ent->bt_body[i] = new btRigidBody(0.0, motionState, cshape, localInertia);
            bt_engine_dynamicsWorld->addRigidBody(ent->bt_body[i], COLLISION_GROUP_CINEMATIC, COLLISION_MASK_ALL);
            ent->bt_body[i]->setUserPointer(ent->self);
        }
    }
}


void Entity_UpdateRoomPos(entity_p ent)
{
    btScalar pos[3], v[3];
    room_p new_room;
    room_sector_p new_sector;

    vec3_add(v, ent->bf.bb_min, ent->bf.bb_max);
    v[0] /= 2.0;
    v[1] /= 2.0;
    v[2] /= 2.0;
    Mat4_vec3_mul_macro(pos, ent->transform, v);
    new_room = Room_FindPosCogerrence(&engine_world, pos, ent->self->room);
    if(new_room)
    {
        if(ent->current_sector)
        {
            Entity_ProcessSector(ent);
        }

        new_sector = Room_GetSectorXYZ(new_room, pos);
        if(new_room != new_sector->owner_room)
        {
            new_room = new_sector->owner_room;
        }

        if(!ent->character && (ent->self->room != new_room))
        {
            if((ent->self->room != NULL) && !Room_IsOverlapped(ent->self->room, new_room))
            {
                if(ent->self->room)
                {
                    Room_RemoveEntity(ent->self->room, ent);
                }
                if(new_room)
                {
                    Room_AddEntity(new_room, ent);
                }
            }
        }

        ent->self->room = new_room;
        ent->last_sector = ent->current_sector;
        
        if(ent->current_sector != new_sector)
        {
            ent->sector_status = 0; // Reset sector status.
            ent->current_sector = new_sector;
        }
    }
}


void Entity_UpdateRigidBody(entity_p ent, int force)
{
    btScalar tr[16];
    btTransform bt_tr;

    if((ent->bf.model == NULL) || (ent->bt_body == NULL) || ((force == 0) && (ent->bf.model->animation_count == 1) && (ent->bf.model->animations->frames_count == 1)))
    {
        return;
    }

    Entity_UpdateRoomPos(ent);

    if(ent->self->collide_flag != 0x00)
    {
        for(uint16_t i=0;i<ent->bf.model->mesh_count;i++)
        {
            if(ent->bt_body[i])
            {
                Mat4_Mat4_mul_macro(tr, ent->transform, ent->bf.bone_tags[i].full_transform);
                bt_tr.setFromOpenGLMatrix(tr);
                ent->bt_body[i]->setCollisionFlags(ent->bt_body[i]->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
                ent->bt_body[i]->setWorldTransform(bt_tr);
            }
        }
    }

    Entity_RebuildBV(ent);
}


void Entity_UpdateRotation(entity_p entity)
{
    btScalar R[4], Rt[4], temp[4];
    btScalar sin_t2, cos_t2, t;
    btScalar *up_dir = entity->transform + 8;                                   // OZ
    btScalar *view_dir = entity->transform + 4;                                 // OY
    btScalar *right_dir = entity->transform + 0;                                // OX
    int i;

    i = entity->angles[0] / 360.0;
    i = (entity->angles[0] < 0.0)?(i-1):(i);
    entity->angles[0] -= 360.0 * i;

    i = entity->angles[1] / 360.0;
    i = (entity->angles[1] < 0.0)?(i-1):(i);
    entity->angles[1] -= 360.0 * i;

    i = entity->angles[2] / 360.0;
    i = (entity->angles[2] < 0.0)?(i-1):(i);
    entity->angles[2] -= 360.0 * i;

    t = entity->angles[0] * M_PI / 180.0;
    sin_t2 = sin(t);
    cos_t2 = cos(t);

    /*
     * LEFT - RIGHT INIT
     */

    view_dir[0] =-sin_t2;                                                       // OY - view
    view_dir[1] = cos_t2;
    view_dir[2] = 0.0;
    view_dir[3] = 0.0;

    right_dir[0] = cos_t2;                                                      // OX - right
    right_dir[1] = sin_t2;
    right_dir[2] = 0.0;
    right_dir[3] = 0.0;

    up_dir[0] = 0.0;                                                            // OZ - up
    up_dir[1] = 0.0;
    up_dir[2] = 1.0;
    up_dir[3] = 0.0;

    if(entity->angles[1] != 0.0)
    {
        t = entity->angles[1] * M_PI / 360.0;                                   // UP - DOWN
        sin_t2 = sin(t);
        cos_t2 = cos(t);
        R[3] = cos_t2;
        R[0] = right_dir[0] * sin_t2;
        R[1] = right_dir[1] * sin_t2;
        R[2] = right_dir[2] * sin_t2;
        vec4_sop(Rt, R);

        vec4_mul(temp, R, up_dir);
        vec4_mul(up_dir, temp, Rt);
        vec4_mul(temp, R, view_dir);
        vec4_mul(view_dir, temp, Rt);
    }

    if(entity->angles[2] != 0.0)
    {
        t = entity->angles[2] * M_PI / 360.0;                                   // ROLL
        sin_t2 = sin(t);
        cos_t2 = cos(t);
        R[3] = cos_t2;
        R[0] = view_dir[0] * sin_t2;
        R[1] = view_dir[1] * sin_t2;
        R[2] = view_dir[2] * sin_t2;
        vec4_sop(Rt, R);

        vec4_mul(temp, R, right_dir);
        vec4_mul(right_dir, temp, Rt);
        vec4_mul(temp, R, up_dir);
        vec4_mul(up_dir, temp, Rt);
    }

    view_dir[3] = 0.0;
    right_dir[3] = 0.0;
    up_dir[3] = 0.0;
}


void Entity_AddOverrideAnim(struct entity_s *ent, int model_id)
{
    skeletal_model_p sm = World_GetModelByID(&engine_world, model_id);

    if((sm != NULL) && (sm->mesh_count == ent->bf.model->mesh_count))
    {
        ss_bone_frame_p bf = (ss_bone_frame_p)malloc(sizeof(ss_bone_frame_t));

        bf->model = sm;
        bf->next = ent->bf.next;
        ent->bf.next = bf;

        bf->frame_time = 0.0;
        bf->next_state = 0;
        bf->lerp = 0.0;
        bf->current_animation = 0;
        bf->current_frame = 0;
        bf->next_animation = 0;
        bf->next_frame = 0;
        bf->period = 1.0 / 30.0;;

        vec3_set_zero(bf->bb_max);
        vec3_set_zero(bf->bb_min);
        vec3_set_zero(bf->centre);
        vec3_set_zero(bf->pos);

        bf->bone_tag_count = sm->mesh_count;
        bf->bone_tags = (ss_bone_tag_p)malloc(bf->bone_tag_count * sizeof(ss_bone_tag_t));
        for(uint16_t i=0;i<bf->bone_tag_count;i++)
        {
            bf->bone_tags[i].flag = sm->mesh_tree[i].flag;
            bf->bone_tags[i].mesh_base = sm->mesh_tree[i].mesh_base;
            bf->bone_tags[i].mesh_skin = sm->mesh_tree[i].mesh_skin;
            bf->bone_tags[i].mesh_slot = NULL;

            vec3_copy(bf->bone_tags[i].offset, sm->mesh_tree[i].offset);
            vec4_set_zero(bf->bone_tags[i].qrotate);
            Mat4_E_macro(bf->bone_tags[i].transform);
            Mat4_E_macro(bf->bone_tags[i].full_transform);
        }
    }
}


void Entity_UpdateCurrentBoneFrame(struct ss_bone_frame_s *bf, btScalar etr[16])
{
    btScalar cmd_tr[3], tr[3];
    ss_bone_tag_p btag = bf->bone_tags;
    bone_tag_p src_btag, next_btag;
    btScalar *stack, *sp, t;
    skeletal_model_p model = bf->model;
    bone_frame_p curr_bf, next_bf;

    next_bf = model->animations[bf->next_animation].frames + bf->next_frame;
    curr_bf = model->animations[bf->current_animation].frames + bf->current_frame;

    t = 1.0 - bf->lerp;
    if(etr && (curr_bf->command & ANIM_CMD_MOVE))
    {
        Mat4_vec3_rot_macro(tr, etr, curr_bf->move);
        vec3_mul_scalar(cmd_tr, tr, bf->lerp);
    }
    else
    {
        vec3_set_zero(tr);
        vec3_set_zero(cmd_tr);
    }

    vec3_interpolate_macro(bf->bb_max, curr_bf->bb_max, next_bf->bb_max, bf->lerp, t);
    vec3_add(bf->bb_max, bf->bb_max, cmd_tr);
    vec3_interpolate_macro(bf->bb_min, curr_bf->bb_min, next_bf->bb_min, bf->lerp, t);
    vec3_add(bf->bb_min, bf->bb_min, cmd_tr);
    vec3_interpolate_macro(bf->centre, curr_bf->centre, next_bf->centre, bf->lerp, t);
    vec3_add(bf->centre, bf->centre, cmd_tr);

    vec3_interpolate_macro(bf->pos, curr_bf->pos, next_bf->pos, bf->lerp, t);
    vec3_add(bf->pos, bf->pos, cmd_tr);
    next_btag = next_bf->bone_tags;
    src_btag = curr_bf->bone_tags;
    for(uint16_t k=0;k<curr_bf->bone_tag_count;k++,btag++,src_btag++,next_btag++)
    {
        vec3_interpolate_macro(btag->offset, src_btag->offset, next_btag->offset, bf->lerp, t);
        vec3_copy(btag->transform+12, btag->offset);
        btag->transform[15] = 1.0;
        if(k == 0)
        {
            btScalar tq[4];
            if(next_bf->command & ANIM_CMD_CHANGE_DIRECTION)
            {
                ///@TODO: add OX rotation inverse for underwater case
                tq[0] =-next_btag->qrotate[1];  // -  +
                tq[1] = next_btag->qrotate[0];  // +  -
                tq[2] = next_btag->qrotate[3];  // +  +
                tq[3] =-next_btag->qrotate[2];  // -  -

                btag->transform[12 + 0] -= bf->pos[0];
                btag->transform[12 + 1] -= bf->pos[1];
                btag->transform[12 + 2] += bf->pos[2];
            }
            else
            {
                vec4_copy(tq, next_btag->qrotate);
                vec3_add(btag->transform+12, btag->transform+12, bf->pos);
            }
            vec4_slerp(btag->qrotate, src_btag->qrotate, tq, bf->lerp);
        }
        else
        {
            bone_tag_p ov_src_btag = src_btag;
            bone_tag_p ov_next_btag = next_btag;
            btScalar ov_lerp = bf->lerp;
            for(ss_bone_frame_p ov_bf=bf->next;ov_bf!=NULL;ov_bf = ov_bf->next)
            {
                if((ov_bf->model != NULL) && (ov_bf->model->mesh_tree[k].replace_anim != 0))
                {
                    bone_frame_p ov_curr_bf = ov_bf->model->animations[ov_bf->current_animation].frames + ov_bf->current_frame;
                    bone_frame_p ov_next_bf = ov_bf->model->animations[ov_bf->next_animation].frames + ov_bf->next_frame;
                    ov_src_btag = ov_curr_bf->bone_tags + k;
                    ov_next_btag = ov_next_bf->bone_tags + k;
                    ov_lerp = ov_bf->lerp;
                    break;
                }
            }
            vec4_slerp(btag->qrotate, ov_src_btag->qrotate, ov_next_btag->qrotate, ov_lerp);
        }
        Mat4_set_qrotation(btag->transform, btag->qrotate);
    }

    /*
     * build absolute coordinate matrix system
     */
    sp = stack = GetTempbtScalar(model->mesh_count * 16);
    int16_t stack_use = 0;

    btag = bf->bone_tags;

    Mat4_Copy(btag->full_transform, btag->transform);
    Mat4_Copy(sp, btag->transform);
    btag++;

    for(uint16_t k=1;k<curr_bf->bone_tag_count;k++,btag++)
    {
        if(btag->flag & 0x01)
        {
            if(stack_use > 0)
            {
                sp -= 16;// glPopMatrix();
                stack_use--;
            }
        }
        if(btag->flag & 0x02)
        {
            if(stack_use + 1 < (int16_t)model->mesh_count)
            {
                Mat4_Copy(sp+16, sp);
                sp += 16;// glPushMatrix();
                stack_use++;
            }
        }
        Mat4_Mat4_mul(sp, sp, btag->transform); // glMultMatrixd(btag->transform);
        Mat4_Copy(btag->full_transform, sp);
    }

    ReturnTempbtScalar(model->mesh_count * 16);
}


int  Entity_GetSubstanceState(entity_p entity)
{
    if((!entity) || (!entity->character))
    {
        return 0;
    }

    if(entity->self->room->flags & TR_ROOM_FLAG_QUICKSAND)
    {
        if(entity->character->height_info.transition_level > entity->transform[12 + 2] + entity->character->Height)
        {
            return ENTITY_SUBSTANCE_QUICKSAND_CONSUMED;
        }
        else
        {
            return ENTITY_SUBSTANCE_QUICKSAND_SHALLOW;
        }
    }
    else if(!entity->character->height_info.water)
    {
        return ENTITY_SUBSTANCE_NONE;
    }
    else if( entity->character->height_info.water &&
            (entity->character->height_info.transition_level > entity->transform[12 + 2]) &&
            (entity->character->height_info.transition_level < entity->transform[12 + 2] + entity->character->wade_depth) )
    {
        return ENTITY_SUBSTANCE_WATER_SHALLOW;
    }
    else if( entity->character->height_info.water &&
            (entity->character->height_info.transition_level > entity->transform[12 + 2] + entity->character->wade_depth) )
    {
        return ENTITY_SUBSTANCE_WATER_WADE;
    }
    else
    {
        return ENTITY_SUBSTANCE_WATER_SWIM;
    }
}

btScalar Entity_FindDistance(entity_p entity_1, entity_p entity_2)
{
    btScalar *v1 = entity_1->transform + 12;
    btScalar *v2 = entity_2->transform + 12;

    return vec3_dist(v1, v2);
}

void Entity_DoAnimCommands(entity_p entity, int changing)
{
    if((engine_world.anim_commands_count == 0) ||
       (entity->bf.model->animations[entity->bf.current_animation].num_anim_commands > 255))
    {
        return;  // If no anim commands or current anim has more than 255 (according to TRosettaStone).
    }

    animation_frame_p af  = entity->bf.model->animations + entity->bf.current_animation;
    uint32_t count        = af->num_anim_commands;
    int16_t *pointer      = engine_world.anim_commands + af->anim_command;
    int8_t   random_value = 0;

    for(uint32_t i = 0; i < count; i++, pointer++)
    {
        switch(*pointer)
        {
            case TR_ANIMCOMMAND_SETPOSITION:
                // This command executes ONLY at the end of animation.
                pointer += 3; // Parse through 3 operands.
                break;

            case TR_ANIMCOMMAND_JUMPDISTANCE:
                // This command executes ONLY at the end of animation.
                pointer += 2; // Parse through 2 operands.
                break;

            case TR_ANIMCOMMAND_EMPTYHANDS:
                ///@FIXME: Behaviour is yet to be discovered.
                break;

            case TR_ANIMCOMMAND_KILL:
                // This command executes ONLY at the end of animation.
                if(entity->bf.current_frame == af->frames_count - 1)
                {
                    if(entity->character)
                    {
                        entity->character->resp.kill = 1;
                    }
                }

                break;

            case TR_ANIMCOMMAND_PLAYSOUND:
                int16_t sound_index;

                if(entity->bf.current_frame == *++pointer)
                {
                    sound_index = *++pointer & 0x3FFF;

                    // Quick workaround for TR3 quicksand.
                    if((Entity_GetSubstanceState(entity) == ENTITY_SUBSTANCE_QUICKSAND_CONSUMED) ||
                       (Entity_GetSubstanceState(entity) == ENTITY_SUBSTANCE_QUICKSAND_SHALLOW)   )
                    {
                        sound_index = 18;
                    }

                    if(*pointer & TR_ANIMCOMMAND_CONDITION_WATER)
                    {
                        if(Entity_GetSubstanceState(entity) == ENTITY_SUBSTANCE_WATER_SHALLOW)
                            Audio_Send(sound_index, TR_AUDIO_EMITTER_ENTITY, entity->id);
                    }
                    else if(*pointer & TR_ANIMCOMMAND_CONDITION_LAND)
                    {
                        if(Entity_GetSubstanceState(entity) != ENTITY_SUBSTANCE_WATER_SHALLOW)
                            Audio_Send(sound_index, TR_AUDIO_EMITTER_ENTITY, entity->id);
                    }
                    else
                    {
                        Audio_Send(sound_index, TR_AUDIO_EMITTER_ENTITY, entity->id);
                    }

                }
                else
                {
                    pointer++;
                }
                break;

            case TR_ANIMCOMMAND_PLAYEFFECT:
                // Effects (flipeffects) are various non-typical actions which vary
                // across different TR game engine versions. There are common ones,
                // however, and currently only these are supported.
                if(entity->bf.current_frame == *++pointer)
                {
                    switch(*++pointer & 0x3FFF)
                    {
                        case TR_EFFECT_SHAKESCREEN:
                            if(engine_world.Character)
                            {
                                btScalar dist = Entity_FindDistance(engine_world.Character, entity);
                                dist = (dist > TR_CAM_MAX_SHAKE_DISTANCE)?(0):((TR_CAM_MAX_SHAKE_DISTANCE - dist) / 1024.0);
                                if(dist > 0)
                                    Cam_Shake(renderer.cam, (dist * TR_CAM_DEFAULT_SHAKE_POWER), 0.5);
                            }
                            break;

                        case TR_EFFECT_CHANGEDIRECTION:
                            break;

                        case TR_EFFECT_HIDEOBJECT:
                            entity->state_flags &= ~ENTITY_STATE_VISIBLE;
                            break;

                        case TR_EFFECT_SHOWOBJECT:
                            entity->state_flags |= ENTITY_STATE_VISIBLE;
                            break;

                        case TR_EFFECT_PLAYSTEPSOUND:
                            // Please note that we bypass land/water mask, as TR3-5 tends to ignore
                            // this flag and play step sound in any case on land, ignoring it
                            // completely in water rooms.
                            if(!Entity_GetSubstanceState(entity))
                            {
                                // TR3-5 footstep map.
                                // We define it here as a magic numbers array, because TR3-5 versions
                                // fortunately have no differences in footstep sounds order.
                                // Also note that some footstep types mutually share same sound IDs
                                // across different TR versions.
                                switch(entity->current_sector->material)
                                {
                                    case SECTOR_MATERIAL_MUD:
                                        Audio_Send(288, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        break;

                                    case SECTOR_MATERIAL_SNOW:  // TR3 & TR5 only
                                        if(engine_world.version != TR_IV)
                                        {
                                            Audio_Send(293, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        }
                                        break;

                                    case SECTOR_MATERIAL_SAND:  // Same as grass
                                        Audio_Send(291, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        break;

                                    case SECTOR_MATERIAL_GRAVEL:
                                        Audio_Send(290, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        break;

                                    case SECTOR_MATERIAL_ICE:   // TR3 & TR5 only
                                        if(engine_world.version != TR_IV)
                                        {
                                            Audio_Send(289, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        }
                                        break;

                                    case SECTOR_MATERIAL_WATER: // BYPASS!
                                        // Audio_Send(17, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        break;

                                    case SECTOR_MATERIAL_STONE: // DEFAULT SOUND, BYPASS!
                                        Audio_Send(-1, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        break;

                                    case SECTOR_MATERIAL_WOOD:
                                        Audio_Send(292, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        break;

                                    case SECTOR_MATERIAL_METAL:
                                        Audio_Send(294, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        break;

                                    case SECTOR_MATERIAL_MARBLE:    // TR4 only
                                        if(engine_world.version == TR_IV)
                                        {
                                            Audio_Send(293, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        }
                                        break;

                                    case SECTOR_MATERIAL_GRASS:     // Same as sand
                                        Audio_Send(291, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        break;

                                    case SECTOR_MATERIAL_CONCRETE:  // DEFAULT SOUND, BYPASS!
                                        Audio_Send(-1, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        break;

                                    case SECTOR_MATERIAL_OLDWOOD:   // Same as wood
                                        Audio_Send(292, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        break;

                                    case SECTOR_MATERIAL_OLDMETAL:  // Same as metal
                                        Audio_Send(294, TR_AUDIO_EMITTER_ENTITY, entity->id);
                                        break;
                                }
                            }
                            break;

                        case TR_EFFECT_BUBBLE:
                            ///@FIXME: Spawn bubble particle here, when particle system is developed.
                            random_value = rand() % 100;
                            if(random_value > 60)
                            {
                                Audio_Send(TR_AUDIO_SOUND_BUBBLE, TR_AUDIO_EMITTER_ENTITY, entity->id);
                            }
                            break;

                        default:
                            ///@FIXME: TODO ALL OTHER EFFECTS!
                            break;
                    }
                }
                else
                {
                    pointer++;
                }
                break;
        }
    }
}

void Entity_ProcessSector(struct entity_s *ent)
{
    if(ent->character)
    {
        ent->character->height_info.walls_climb_dir = ent->current_sector->flags & (SECTOR_FLAG_CLIMB_WEST  |
                                                                                    SECTOR_FLAG_CLIMB_EAST  |
                                                                                    SECTOR_FLAG_CLIMB_NORTH |
                                                                                    SECTOR_FLAG_CLIMB_SOUTH );

        ent->character->height_info.walls_climb     = (ent->character->height_info.walls_climb_dir > 0);
        ent->character->height_info.ceiling_climb   = 0x00;

        for(room_sector_p rs=ent->current_sector;rs!=NULL;rs=rs->sector_above)
        {
            if(rs->flags & SECTOR_FLAG_CLIMB_CEILING)
            {
                ent->character->height_info.ceiling_climb = 0x01;
                break;
            }
        }

        if(ent->character->height_info.ceiling_climb == 0x00)
        {
            for(room_sector_p rs = ent->current_sector->sector_below;rs!=NULL;rs=rs->sector_below)
            {
                if(rs->flags & SECTOR_FLAG_CLIMB_CEILING)
                {
                    ent->character->height_info.ceiling_climb = 0x01;
                    break;
                }
            }
        }

        if(ent->current_sector->flags & SECTOR_FLAG_DEATH)
        {
            if((ent->move_type == MOVE_ON_FLOOR)    ||
               (ent->move_type == MOVE_UNDER_WATER) ||
               (ent->move_type == MOVE_WADE)        ||
               (ent->move_type == MOVE_ON_WATER)    ||
               (ent->move_type == MOVE_QUICKSAND))
            {
                Character_SetParam(ent, PARAM_HEALTH, 0.0);
                ent->character->resp.kill = 1;
            }
        }
    }
    
    // Look up trigger function table and run trigger if it exists.
    
    lua_getglobal(engine_lua, "tlist_RunTrigger");
    if(lua_isfunction(engine_lua, -1))
    {
        lua_pushnumber(engine_lua, ent->current_sector->trig_index);
        lua_pushnumber(engine_lua, ((ent->bf.model->id == 0) ? TR_ACTIVATORTYPE_LARA : TR_ACTIVATORTYPE_MISC));
        lua_pushnumber(engine_lua, ent->id);
        lua_pcall(engine_lua, 3, 1, 0);
        int result = lua_tointeger(engine_lua, -1);
        lua_pop(engine_lua, 1);
    }
}


void Entity_SetAnimation(entity_p entity, int animation, int frame)
{
    if(!entity || !entity->bf.model || (animation >= entity->bf.model->animation_count))
    {
        return;
    }

    animation = (animation < 0)?(0):(animation);

    if(entity->character)
    {
        entity->character->no_fix = 0x00;
    }

    entity->bf.lerp = 0.0;
    animation_frame_p anim = &entity->bf.model->animations[animation];
    frame %= anim->frames_count;
    frame = (frame >= 0)?(frame):(anim->frames_count - 1 + frame);
    entity->bf.period = 1.0 / 30.0;

    entity->bf.last_state = anim->state_id;
    entity->bf.next_state = anim->state_id;
    entity->current_speed = anim->speed;
    entity->bf.current_animation = animation;
    entity->bf.current_frame = frame;
    entity->bf.next_animation = animation;
    entity->bf.next_frame = frame;

    entity->bf.frame_time = (btScalar)frame * entity->bf.period;
    long int t = (entity->bf.frame_time) / entity->bf.period;
    btScalar dt = entity->bf.frame_time - (btScalar)t * entity->bf.period;
    entity->bf.frame_time = (btScalar)frame * entity->bf.period + dt;

    Entity_UpdateCurrentBoneFrame(&entity->bf, entity->transform);
    Entity_UpdateRigidBody(entity, 0);
}


struct state_change_s *Anim_FindStateChangeByAnim(struct animation_frame_s *anim, int state_change_anim)
{
    if(state_change_anim >= 0)
    {
        state_change_p ret = anim->state_change;
        for(uint16_t i=0;i<anim->state_change_count;i++,ret++)
        {
            for(uint16_t j=0;j<ret->anim_dispatch_count;j++)
            {
                if(ret->anim_dispatch[j].next_anim == state_change_anim)
                {
                    return ret;
                }
            }
        }
    }

    return NULL;
}


struct state_change_s *Anim_FindStateChangeByID(struct animation_frame_s *anim, uint32_t id)
{
    state_change_p ret = anim->state_change;
    for(uint16_t i=0;i<anim->state_change_count;i++,ret++)
    {
        if(ret->id == id)
        {
            return ret;
        }
    }

    return NULL;
}


int Entity_GetAnimDispatchCase(struct entity_s *entity, uint32_t id)
{
    animation_frame_p anim = entity->bf.model->animations + entity->bf.current_animation;
    state_change_p stc = anim->state_change;
    
    for(uint16_t i=0;i<anim->state_change_count;i++,stc++)
    {
        if(stc->id == id)
        {
            anim_dispatch_p disp = stc->anim_dispatch;
            for(uint16_t j=0;j<stc->anim_dispatch_count;j++,disp++)
            {
                if((disp->frame_high >= disp->frame_low) && (entity->bf.current_frame >= disp->frame_low) && (entity->bf.current_frame <= disp->frame_high))// ||
                   //(disp->frame_high <  disp->frame_low) && ((entity->bf.current_frame >= disp->frame_low) || (entity->bf.current_frame <= disp->frame_high)))
                {
                    return (int)j;
                }
            }
        }
    }

    return -1;
}

/*
 * Next frame and next anim calculation function.
 */
void Entity_GetNextFrame(struct ss_bone_frame_s *bf, btScalar time, struct state_change_s *stc, int16_t *frame, int16_t *anim, uint16_t anim_flags)
{
    animation_frame_p curr_anim = bf->model->animations + bf->current_animation;

    *frame = (bf->frame_time + time) / bf->period;
    *frame = (*frame >= 0.0)?(*frame):(0.0);                                    // paranoid checking
    *anim  = bf->current_animation;

    /*
     * Flag has a highest priority
     */
    if(anim_flags == ANIM_LOOP_LAST_FRAME)
    {
        if(*frame >= curr_anim->frames_count - 1)
        {
            *frame = curr_anim->frames_count - 1;
            *anim  = bf->current_animation;                                     // paranoid dublicate
        }
        return;
    }

    /*
     * Check next anim if frame >= frames_count
     */
    if(*frame >= curr_anim->frames_count)
    {
        if(curr_anim->next_anim)
        {
            *frame = curr_anim->next_frame;
            *anim  = curr_anim->next_anim->id;
            return;
        }

        *frame %= curr_anim->frames_count;
        *anim   = bf->current_animation;                                      // paranoid dublicate
        return;
    }

    /*
     * State change check
     */
    if(stc != NULL)
    {
        anim_dispatch_p disp = stc->anim_dispatch;
        for(uint16_t i=0;i<stc->anim_dispatch_count;i++,disp++)
        {
            if((disp->frame_high >= disp->frame_low) && (*frame >= disp->frame_low) && (*frame <= disp->frame_high))
            {
                *anim  = disp->next_anim;
                *frame = disp->next_frame;
                //*frame = (disp->next_frame + (*frame - disp->frame_low)) % bf->model->animations[disp->next_anim].frames_count;
                return;                                                         // anim was changed
            }
        }
    }
}


void Entity_DoAnimMove(entity_p entity)
{
    if(entity->bf.model != NULL)
    {
        bone_frame_p curr_bf = entity->bf.model->animations[entity->bf.current_animation].frames + entity->bf.current_frame;

        if(curr_bf->command & ANIM_CMD_JUMP)
        {
            Character_SetToJump(entity, -curr_bf->v_Vertical, curr_bf->v_Horizontal);
        }
        if(curr_bf->command & ANIM_CMD_CHANGE_DIRECTION)
        {
            entity->angles[0] += 180.0;
            if(entity->move_type == MOVE_UNDER_WATER)
            {
                entity->angles[1] = -entity->angles[1];                         // for underwater case
            }
            if(entity->dir_flag == ENT_MOVE_BACKWARD)
            {
                entity->dir_flag = ENT_MOVE_FORWARD;
            }
            else if(entity->dir_flag == ENT_MOVE_FORWARD)
            {
                entity->dir_flag = ENT_MOVE_BACKWARD;
            }
            Entity_UpdateRotation(entity);
        }
        if(curr_bf->command & ANIM_CMD_MOVE)
        {
            btScalar tr[3];
            Mat4_vec3_rot_macro(tr, entity->transform, curr_bf->move);
            vec3_add(entity->transform+12, entity->transform+12, tr);
        }
    }
}


#define WEAPON_STATE_HIDE               (0x00)
#define WEAPON_STATE_HIDE_TO_READY      (0x01)
#define WEAPON_STATE_IDLE               (0x02)
#define WEAPON_STATE_IDLE_TO_FIRE       (0x03)
#define WEAPON_STATE_FIRE               (0x04)
#define WEAPON_STATE_FIRE_TO_IDLE       (0x05)
#define WEAPON_STATE_IDLE_TO_HIDE       (0x06)

/**
 * In original engine (+ some information from anim_commands) the anim_commands implement in beginning of frame
 */
int Entity_Frame(entity_p entity, btScalar time)
{
    int16_t frame, anim, ret = 0x00;
    long int t;
    btScalar dt;
    animation_frame_p af;
    state_change_p stc;

    if((entity == NULL) || !(entity->state_flags & ENTITY_STATE_ACTIVE)  || !(entity->state_flags & ENTITY_STATE_ENABLED) || (entity->bf.model == NULL) || ((entity->bf.model->animation_count == 1) && (entity->bf.model->animations->frames_count == 1)))
    {
        return 0;
    }

    entity->bf.lerp = 0.0;
    stc = Anim_FindStateChangeByID(entity->bf.model->animations + entity->bf.current_animation, entity->bf.next_state);
    Entity_GetNextFrame(&entity->bf, time, stc, &frame, &anim, entity->anim_flags);
    if(anim != entity->bf.current_animation)
    {
        entity->bf.last_animation = entity->bf.current_animation;

        ret = 0x02;
        Entity_DoAnimCommands(entity, ret);
        Entity_DoAnimMove(entity);
        Entity_SetAnimation(entity, anim, frame);
        stc = Anim_FindStateChangeByID(entity->bf.model->animations + entity->bf.current_animation, entity->bf.next_state);
    }
    else if(entity->bf.current_frame != frame)
    {
        if(entity->bf.current_frame == 0)
        {
            entity->bf.last_animation = entity->bf.current_animation;
        }

        ret = 0x01;
        Entity_DoAnimCommands(entity, ret);
        Entity_DoAnimMove(entity);
        entity->bf.current_frame = frame;
    }

    af = entity->bf.model->animations + entity->bf.current_animation;
    entity->bf.frame_time += time;

    t = (entity->bf.frame_time) / entity->bf.period;
    dt = entity->bf.frame_time - (btScalar)t * entity->bf.period;
    entity->bf.frame_time = (btScalar)frame * entity->bf.period + dt;
    entity->bf.lerp = (entity->smooth_anim)?(dt / entity->bf.period):(0.0);
    Entity_GetNextFrame(&entity->bf, entity->bf.period, stc, &entity->bf.next_frame, &entity->bf.next_animation, entity->anim_flags);

    /* There are stick code for multianimation (weapon mode) testing
     * Model replacing will be upgraded too, I have to add override
     * flags to model manually in the script*/
    if(entity->character != NULL)
    {
        /* anims (TR_I - TR_V):
         * pistols:
         * 0: idle to fire;
         * 1: draw weapon (short?);
         * 2: draw weapon (full);
         * 3: fire process;
         *
         * shotgun, rifles, crossbow, harpoon, launchers (2 handed weapons):
         * 0: idle to fire;
         * 1: draw weapon;
         * 2: fire process;
         * 3: hide weapon;
         * 4: idle to fire (targeted);
         */
        if((entity->character->cmd.ready_weapon != 0x00) && (entity->character->current_weapon > 0) && (entity->character->weapon_current_state == WEAPON_STATE_HIDE))
        {
            Character_SetWeaponModel(entity, entity->character->current_weapon, 1);
        }

        for(ss_bone_frame_p bf=entity->bf.next;bf!=NULL;bf=bf->next)
        {
            if((bf->model != NULL) && (bf->model->animation_count > 4))
            {
                switch(entity->character->weapon_current_state)
                {
                    case WEAPON_STATE_HIDE:
                        if(entity->character->cmd.ready_weapon)   // ready weapon
                        {
                            bf->current_animation = 1;
                            bf->next_animation = 1;
                            bf->current_frame = 0;
                            bf->next_frame = 0;
                            bf->frame_time = 0.0;
                            entity->character->weapon_current_state = WEAPON_STATE_HIDE_TO_READY;
                        }
                        break;

                    case WEAPON_STATE_HIDE_TO_READY:
                        bf->frame_time += time;
                        bf->current_frame = (bf->frame_time) / bf->period;
                        dt = bf->frame_time - (btScalar)bf->current_frame * bf->period;
                        bf->lerp = (entity->smooth_anim)?(dt / bf->period):(0.0);
                        t = bf->model->animations[bf->current_animation].frames_count;

                        if(bf->current_frame < t - 1)
                        {
                            bf->next_frame = (bf->current_frame + 1) % t;
                            bf->next_animation = bf->current_animation;
                        }
                        else if(bf->current_frame < t)
                        {
                            bf->next_frame = 0;
                            bf->next_animation = 0;
                        }
                        else
                        {
                            bf->current_frame = 0;
                            bf->current_animation = 0;
                            bf->next_frame = 0;
                            bf->next_animation = 0;
                            bf->frame_time = 0.0;
                            entity->character->weapon_current_state = WEAPON_STATE_IDLE;
                        }
                        break;

                    case WEAPON_STATE_IDLE:
                        bf->current_frame = 0;
                        bf->current_animation = 0;
                        bf->next_frame = 0;
                        bf->next_animation = 0;
                        bf->frame_time = 0.0;
                        if(entity->character->cmd.ready_weapon)
                        {
                            bf->current_animation = 3;
                            bf->next_animation = 3;
                            bf->current_frame = bf->next_frame = 0;
                            bf->frame_time = 0.0;
                            entity->character->weapon_current_state = WEAPON_STATE_IDLE_TO_HIDE;
                        }
                        else if(entity->character->cmd.action)
                        {
                            entity->character->weapon_current_state = WEAPON_STATE_IDLE_TO_FIRE;
                        }
                        else
                        {
                            // do nothing here, may be;
                        }
                        break;

                    case WEAPON_STATE_FIRE_TO_IDLE:
                        // Yes, same animation, reverse frames order;
                        t = bf->model->animations[bf->current_animation].frames_count;
                        bf->frame_time += time;
                        bf->current_frame = (bf->frame_time) / bf->period;
                        dt = bf->frame_time - (btScalar)bf->current_frame * bf->period;
                        bf->lerp = (entity->smooth_anim)?(dt / bf->period):(0.0);
                        bf->current_frame = t - 1 - bf->current_frame;
                        if(bf->current_frame > 0)
                        {
                            bf->next_frame = bf->current_frame - 1;
                            bf->next_animation = bf->current_animation;
                        }
                        else
                        {
                            bf->next_frame = bf->current_frame = 0;
                            bf->next_animation = bf->current_animation;
                            entity->character->weapon_current_state = WEAPON_STATE_IDLE;
                        }
                        break;

                    case WEAPON_STATE_IDLE_TO_FIRE:
                        bf->frame_time += time;
                        bf->current_frame = (bf->frame_time) / bf->period;
                        dt = bf->frame_time - (btScalar)bf->current_frame * bf->period;
                        bf->lerp = (entity->smooth_anim)?(dt / bf->period):(0.0);
                        t = bf->model->animations[bf->current_animation].frames_count;

                        if(bf->current_frame < t - 1)
                        {
                            bf->next_frame = bf->current_frame + 1;
                            bf->next_animation = bf->current_animation;
                        }
                        else if(bf->current_frame < t)
                        {
                            bf->next_frame = 0;
                            bf->next_animation = 2;
                        }
                        else if(entity->character->cmd.action)
                        {
                            bf->current_frame = 0;
                            bf->next_frame = 1;
                            bf->current_animation = 2;
                            bf->next_animation = bf->current_animation;
                            entity->character->weapon_current_state = WEAPON_STATE_FIRE;
                        }
                        else
                        {
                            bf->frame_time = 0.0;
                            bf->current_frame = bf->model->animations[bf->current_animation].frames_count - 1;
                            entity->character->weapon_current_state = WEAPON_STATE_FIRE_TO_IDLE;
                        }
                        break;

                    case WEAPON_STATE_FIRE:
                        if(entity->character->cmd.action)
                        {
                            // inc time, loop;
                            bf->frame_time += time;
                            bf->current_frame = (bf->frame_time) / bf->period;
                            dt = bf->frame_time - (btScalar)bf->current_frame * bf->period;
                            bf->lerp = (entity->smooth_anim)?(dt / bf->period):(0.0);
                            t = bf->model->animations[bf->current_animation].frames_count;

                            if(bf->current_frame < t - 1)
                            {
                                bf->next_frame = bf->current_frame + 1;
                                bf->next_animation = bf->current_animation;
                            }
                            else if(bf->current_frame < t)
                            {
                                bf->next_frame = 0;
                                bf->next_animation = bf->current_animation;
                            }
                            else
                            {
                                bf->frame_time = dt;
                                bf->current_frame = 0;
                                bf->next_frame = 1;
                            }
                        }
                        else
                        {
                            bf->frame_time = 0.0;
                            bf->current_animation = 0;
                            bf->next_animation = bf->current_animation;
                            bf->current_frame = bf->model->animations[bf->current_animation].frames_count - 1;
                            bf->next_frame = (bf->current_frame > 0)?(bf->current_frame - 1):(0);
                            entity->character->weapon_current_state = WEAPON_STATE_FIRE_TO_IDLE;
                        }
                        break;

                    case WEAPON_STATE_IDLE_TO_HIDE:
                        t = bf->model->animations[bf->current_animation].frames_count;
                        bf->frame_time += time;
                        bf->current_frame = (bf->frame_time) / bf->period;
                        dt = bf->frame_time - (btScalar)bf->current_frame * bf->period;
                        bf->lerp = (entity->smooth_anim)?(dt / bf->period):(0.0);
                        if(bf->current_frame < t - 1)
                        {
                            bf->next_frame = bf->current_frame + 1;
                            bf->next_animation = bf->current_animation;
                        }
                        else
                        {
                            bf->next_frame = bf->current_frame = 0;
                            bf->next_animation = bf->current_animation;
                            entity->character->weapon_current_state = WEAPON_STATE_HIDE;
                            Character_SetWeaponModel(entity, entity->character->current_weapon, 0);
                        }
                        break;
                };
            }
            else if((bf->model != NULL) && (bf->model->animation_count == 4))
            {
                switch(entity->character->weapon_current_state)
                {
                    case WEAPON_STATE_HIDE:
                        if(entity->character->cmd.ready_weapon)   // ready weapon
                        {
                            bf->current_animation = 2;
                            bf->next_animation = 2;
                            bf->current_frame = 0;
                            bf->next_frame = 0;
                            bf->frame_time = 0.0;
                            entity->character->weapon_current_state = WEAPON_STATE_HIDE_TO_READY;
                        }
                        break;

                    case WEAPON_STATE_HIDE_TO_READY:
                        bf->frame_time += time;
                        bf->current_frame = (bf->frame_time) / bf->period;
                        dt = bf->frame_time - (btScalar)bf->current_frame * bf->period;
                        bf->lerp = (entity->smooth_anim)?(dt / bf->period):(0.0);
                        t = bf->model->animations[bf->current_animation].frames_count;

                        if(bf->current_frame < t - 1)
                        {
                            bf->next_frame = (bf->current_frame + 1) % t;
                            bf->next_animation = bf->current_animation;
                        }
                        else if(bf->current_frame < t)
                        {
                            bf->next_frame = 0;
                            bf->next_animation = 0;
                        }
                        else
                        {
                            bf->current_frame = 0;
                            bf->current_animation = 0;
                            bf->next_frame = 0;
                            bf->next_animation = 0;
                            bf->frame_time = 0.0;
                            entity->character->weapon_current_state = WEAPON_STATE_IDLE;
                        }
                        break;

                    case WEAPON_STATE_IDLE:
                        bf->current_frame = 0;
                        bf->current_animation = 0;
                        bf->next_frame = 0;
                        bf->next_animation = 0;
                        bf->frame_time = 0.0;
                        if(entity->character->cmd.ready_weapon)
                        {
                            bf->current_animation = 2;
                            bf->next_animation = 2;
                            bf->current_frame = bf->next_frame = bf->model->animations[bf->current_animation].frames_count - 1;
                            bf->frame_time = 0.0;
                            entity->character->weapon_current_state = WEAPON_STATE_IDLE_TO_HIDE;
                        }
                        else if(entity->character->cmd.action)
                        {
                            entity->character->weapon_current_state = WEAPON_STATE_IDLE_TO_FIRE;
                        }
                        else
                        {
                            // do nothing here, may be;
                        }
                        break;

                    case WEAPON_STATE_FIRE_TO_IDLE:
                        // Yes, same animation, reverse frames order;
                        t = bf->model->animations[bf->current_animation].frames_count;
                        bf->frame_time += time;
                        bf->current_frame = (bf->frame_time) / bf->period;
                        dt = bf->frame_time - (btScalar)bf->current_frame * bf->period;
                        bf->lerp = (entity->smooth_anim)?(dt / bf->period):(0.0);
                        bf->current_frame = t - 1 - bf->current_frame;
                        if(bf->current_frame > 0)
                        {
                            bf->next_frame = bf->current_frame - 1;
                            bf->next_animation = bf->current_animation;
                        }
                        else
                        {
                            bf->next_frame = bf->current_frame = 0;
                            bf->next_animation = bf->current_animation;
                            entity->character->weapon_current_state = WEAPON_STATE_IDLE;
                        }
                        break;

                    case WEAPON_STATE_IDLE_TO_FIRE:
                        bf->frame_time += time;
                        bf->current_frame = (bf->frame_time) / bf->period;
                        dt = bf->frame_time - (btScalar)bf->current_frame * bf->period;
                        bf->lerp = (entity->smooth_anim)?(dt / bf->period):(0.0);
                        t = bf->model->animations[bf->current_animation].frames_count;

                        if(bf->current_frame < t - 1)
                        {
                            bf->next_frame = bf->current_frame + 1;
                            bf->next_animation = bf->current_animation;
                        }
                        else if(bf->current_frame < t)
                        {
                            bf->next_frame = 0;
                            bf->next_animation = 3;
                        }
                        else if(entity->character->cmd.action)
                        {
                            bf->current_frame = 0;
                            bf->next_frame = 1;
                            bf->current_animation = 3;
                            bf->next_animation = bf->current_animation;
                            entity->character->weapon_current_state = WEAPON_STATE_FIRE;
                        }
                        else
                        {
                            bf->frame_time = 0.0;
                            bf->current_frame = bf->model->animations[bf->current_animation].frames_count - 1;
                            entity->character->weapon_current_state = WEAPON_STATE_FIRE_TO_IDLE;
                        }
                        break;

                    case WEAPON_STATE_FIRE:
                        if(entity->character->cmd.action)
                        {
                            // inc time, loop;
                            bf->frame_time += time;
                            bf->current_frame = (bf->frame_time) / bf->period;
                            dt = bf->frame_time - (btScalar)bf->current_frame * bf->period;
                            bf->lerp = (entity->smooth_anim)?(dt / bf->period):(0.0);
                            t = bf->model->animations[bf->current_animation].frames_count;

                            if(bf->current_frame < t - 1)
                            {
                                bf->next_frame = bf->current_frame + 1;
                                bf->next_animation = bf->current_animation;
                            }
                            else if(bf->current_frame < t)
                            {
                                bf->next_frame = 0;
                                bf->next_animation = bf->current_animation;
                            }
                            else
                            {
                                bf->frame_time = dt;
                                bf->current_frame = 0;
                                bf->next_frame = 1;
                            }
                        }
                        else
                        {
                            bf->frame_time = 0.0;
                            bf->current_animation = 0;
                            bf->next_animation = bf->current_animation;
                            bf->current_frame = bf->model->animations[bf->current_animation].frames_count - 1;
                            bf->next_frame = (bf->current_frame > 0)?(bf->current_frame - 1):(0);
                            entity->character->weapon_current_state = WEAPON_STATE_FIRE_TO_IDLE;
                        }
                        break;

                    case WEAPON_STATE_IDLE_TO_HIDE:
                        // Yes, same animation, reverse frames order;
                        t = bf->model->animations[bf->current_animation].frames_count;
                        bf->frame_time += time;
                        bf->current_frame = (bf->frame_time) / bf->period;
                        dt = bf->frame_time - (btScalar)bf->current_frame * bf->period;
                        bf->lerp = (entity->smooth_anim)?(dt / bf->period):(0.0);
                        bf->current_frame = t - 1 - bf->current_frame;
                        if(bf->current_frame > 0)
                        {
                            bf->next_frame = bf->current_frame - 1;
                            bf->next_animation = bf->current_animation;
                        }
                        else
                        {
                            bf->next_frame = bf->current_frame = 0;
                            bf->next_animation = bf->current_animation;
                            entity->character->weapon_current_state = WEAPON_STATE_HIDE;
                            Character_SetWeaponModel(entity, entity->character->current_weapon, 0);
                        }
                        break;
                };
            }
        }
    }

    /*
     * Update acceleration
     */
    if(entity->character)
    {
        entity->current_speed += time * entity->character->speed_mult * (btScalar)af->accel_hi;
    }

    Entity_UpdateCurrentBoneFrame(&entity->bf, entity->transform);
    if(entity->onFrame != NULL)
    {
        entity->onFrame(entity, ret);
    }

    return ret;
}

/**
 * The function rebuild / renew entity's BV
 */
void Entity_RebuildBV(entity_p ent)
{
    if((ent != NULL) && (ent->bf.model != NULL))
    {
        /*
         * get current BB from animation
         */
        OBB_Rebuild(ent->obb, ent->bf.bb_min, ent->bf.bb_max);
        OBB_Transform(ent->obb);
    }
}


void Entity_CheckActivators(struct entity_s *ent)
{
    if((ent != NULL) && (ent->self->room != NULL))
    {
        btScalar ppos[3];

        ppos[0] = ent->transform[12+0] + ent->transform[4+0] * ent->bf.bb_max[1];
        ppos[1] = ent->transform[12+1] + ent->transform[4+1] * ent->bf.bb_max[1];
        ppos[2] = ent->transform[12+2] + ent->transform[4+2] * ent->bf.bb_max[1];
        engine_container_p cont = ent->self->room->containers;
        for(;cont;cont=cont->next)
        {
            if((cont->object_type == OBJECT_ENTITY) && (cont->object))
            {
                entity_p e = (entity_p)cont->object;
                btScalar r = e->activation_offset[3];
                r *= r;
                if((e->type_flags & ENTITY_TYPE_INTERACTIVE) && (e->state_flags & ENTITY_STATE_ENABLED))
                {
                    //Mat4_vec3_mul_macro(pos, e->transform, e->activation_offset);
                    if((e != ent) && (OBB_OBB_Test(e, ent) == 1))//(vec3_dist_sq(ent->transform+12, pos) < r))
                    {
                        lua_ExecEntity(engine_lua, e->id, ent->id, ENTITY_CALLBACK_ACTIVATE);
                    }
                }
                else if((e->type_flags & ENTITY_TYPE_PICKABLE) && (e->state_flags & ENTITY_STATE_ENABLED))
                {
                    btScalar *v = e->transform + 12;
                    if((e != ent) && ((v[0] - ppos[0]) * (v[0] - ppos[0]) + (v[1] - ppos[1]) * (v[1] - ppos[1]) < r) &&
                                      (v[2] + 32.0 > ent->transform[12+2] + ent->bf.bb_min[2]) && (v[2] - 32.0 < ent->transform[12+2] + ent->bf.bb_max[2]))
                    {
                        lua_ExecEntity(engine_lua, e->id, ent->id, ENTITY_CALLBACK_ACTIVATE);
                    }
                }
            }
        }
    }
}


void Entity_MoveForward(struct entity_s *ent, btScalar dist)
{
    ent->transform[12] += ent->transform[4] * dist;
    ent->transform[13] += ent->transform[5] * dist;
    ent->transform[14] += ent->transform[6] * dist;
}


void Entity_MoveStrafe(struct entity_s *ent, btScalar dist)
{
    ent->transform[12] += ent->transform[0] * dist;
    ent->transform[13] += ent->transform[1] * dist;
    ent->transform[14] += ent->transform[2] * dist;
}


void Entity_MoveVertical(struct entity_s *ent, btScalar dist)
{
    ent->transform[12] += ent->transform[8] * dist;
    ent->transform[13] += ent->transform[9] * dist;
    ent->transform[14] += ent->transform[10] * dist;
}
