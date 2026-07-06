/*
 * PureRetro — Storage-oriented libretro environment callbacks
 */

#include "core_internal.h"

#include "log.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

/* Apply --disk-index after the core has registered a disk control interface.
 * The libretro contract permits set_image_index() at any time after init. */
static void disk_control_apply_initial_index(struct frontend_state *fe)
{
    if (!fe->has_disk_control)
        return;
    if (fe->initial_disk_index < 0)
        return;

    const struct retro_disk_control_ext_callback *d = &fe->disk_control;
    if (!d->get_num_images || !d->set_eject_state || !d->set_image_index) {
        LOG_WARN("--disk-index ignored: core missing required disk callbacks");
        return;
    }

    unsigned num = d->get_num_images();
    unsigned idx = (unsigned)fe->initial_disk_index;
    if (idx >= num) {
        LOG_WARN("--disk-index %u out of range (core reports %u images)",
                 idx, num);
        return;
    }

    /* Standard eject-set-insert dance, mirroring how real frontends switch
     * disks. Failures are warned but non-fatal: the core may simply have
     * the requested disk already loaded. */
    if (!d->set_eject_state(true))
        LOG_WARN("disk set_eject_state(true) failed");
    if (!d->set_image_index(idx))
        LOG_WARN("disk set_image_index(%u) failed", idx);
    if (!d->set_eject_state(false))
        LOG_WARN("disk set_eject_state(false) failed");

    LOG_INFO("Disk index set to %u (of %u)", idx, num);
}


bool env_set_disk_control_interface(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_disk_control_interface: NULL data"); return false; }
    const struct retro_disk_control_callback *legacy =
        (const struct retro_disk_control_callback *)data;
    /* The legacy struct's first 7 fields are layout-identical to the
     * ext struct. memcpy those and leave the ext-only fields NULL
     * (memset guarantees set_initial_image/get_image_path/get_image_label). */
    memset(&fe->disk_control, 0, sizeof(fe->disk_control));
    memcpy(&fe->disk_control, legacy,
           sizeof(struct retro_disk_control_callback));
    fe->has_disk_control = true;
    LOG_INFO("Core registered legacy disk control interface");
    disk_control_apply_initial_index(fe);
    return true;
}

bool env_set_controller_info(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_controller_info: NULL data"); return false; }
    const struct retro_controller_info *info =
        (const struct retro_controller_info *)data;

    core_controller_ports_clear(fe);

    unsigned port = 0;
    for (const struct retro_controller_info *p = info;
         p && p->types && p->num_types > 0;
         ++p, ++port) {
        if (port >= FRONTEND_MAX_PORTS) {
            LOG_WARN("SET_CONTROLLER_INFO: dropping ports beyond %u",
                     (unsigned)FRONTEND_MAX_PORTS);
            break;
        }

        struct controller_port_info *slot = &fe->controller_ports[port];
        slot->types = calloc(p->num_types, sizeof(*slot->types));
        if (!slot->types) {
            core_controller_ports_clear(fe);
            return false;
        }
        slot->num_types = p->num_types;

        for (unsigned i = 0; i < p->num_types; ++i) {
            slot->types[i].id = p->types[i].id;
            slot->types[i].desc = p->types[i].desc
                ? SDL_strdup(p->types[i].desc) : NULL;
        }
    }
    fe->controller_port_count = port;

    LOG_INFO("Core registered controller info for %u port(s):", port);
    for (unsigned i = 0; i < port; ++i) {
        const struct controller_port_info *slot = &fe->controller_ports[i];
        LOG_INFO("  port %u: %u device type(s)", i, slot->num_types);
        for (unsigned t = 0; t < slot->num_types; ++t) {
            LOG_INFO("    [%u] id=%u desc=%s",
                     t, slot->types[t].id,
                     slot->types[t].desc ? slot->types[t].desc : "(null)");
        }
    }
    return true;
}

bool env_set_disk_control_ext_interface(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_disk_control_ext_interface: NULL data"); return false; }
    const struct retro_disk_control_ext_callback *cb =
        (const struct retro_disk_control_ext_callback *)data;
    fe->disk_control = *cb;
    fe->has_disk_control = true;
    LOG_INFO("Core registered disk control ext interface (num_images=%u)",
             cb->get_num_images ? cb->get_num_images() : 0);
    disk_control_apply_initial_index(fe);
    return true;
}

bool env_set_serialization_quirks(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_set_serialization_quirks: NULL data"); return false; }
    uint64_t *quirks = (uint64_t *)data;
    /* The frontend does not require the core to drop any quirks, so we
     * leave whatever the core wrote in place. Log the declared bits so
     * savestate misbehavior is easier to attribute. */
    LOG_INFO("Core serialization quirks: 0x%llx",
             (unsigned long long)*quirks);
    return true;
}

bool env_set_hw_shared_context(struct frontend_state *fe, void *data)
{
    (void)data;
    fe->video.hw_shared_context_requested = true;
    LOG_INFO("Core requested shared GL context");
    return true;
}

bool env_set_subsystem_info(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_subsystem_info: NULL data"); return false; }
    const struct retro_subsystem_info *list =
        (const struct retro_subsystem_info *)data;

    core_subsystem_info_clear(fe);

    /* Count entries first (terminated by a zeroed-out struct). */
    unsigned count = 0;
    for (const struct retro_subsystem_info *p = list;
         p && (p->desc || p->ident || p->roms || p->num_roms);
         ++p)
        count++;

    if (count == 0) {
        LOG_INFO("Core registered 0 subsystems");
        return true;
    }

    fe->subsystem_info = calloc(count,
        sizeof(*fe->subsystem_info));
    if (!fe->subsystem_info)
        return false;
    fe->subsystem_info_count = count;

    for (unsigned i = 0; i < count; ++i) {
        const struct retro_subsystem_info *src = &list[i];
        struct subsystem_storage *dst = &fe->subsystem_info[i];
        dst->desc  = src->desc  ? SDL_strdup(src->desc)  : NULL;
        dst->ident = src->ident ? SDL_strdup(src->ident) : NULL;
        dst->id    = src->id;
        dst->num_roms = src->num_roms;
        if (src->num_roms == 0)
            continue;

        dst->roms = calloc(src->num_roms, sizeof(*dst->roms));
        if (!dst->roms) {
            core_subsystem_info_clear(fe);
            return false;
        }

        for (unsigned j = 0; j < src->num_roms; ++j) {
            const struct retro_subsystem_rom_info *srom = &src->roms[j];
            struct subsystem_rom_storage *drom = &dst->roms[j];
            drom->desc             = srom->desc
                                     ? SDL_strdup(srom->desc) : NULL;
            drom->valid_extensions = srom->valid_extensions
                                     ? SDL_strdup(srom->valid_extensions)
                                     : NULL;
            drom->need_fullpath    = srom->need_fullpath;
            drom->block_extract    = srom->block_extract;
            drom->required         = srom->required;
            drom->num_memory       = srom->num_memory;
            if (srom->num_memory == 0)
                continue;

            drom->memory = calloc(srom->num_memory, sizeof(*drom->memory));
            drom->memory_extensions =
                calloc(srom->num_memory, sizeof(*drom->memory_extensions));
            if (!drom->memory || !drom->memory_extensions) {
                core_subsystem_info_clear(fe);
                return false;
            }
            for (unsigned k = 0; k < srom->num_memory; ++k) {
                drom->memory_extensions[k] = srom->memory[k].extension
                    ? SDL_strdup(srom->memory[k].extension) : NULL;
                drom->memory[k].extension = drom->memory_extensions[k];
                drom->memory[k].type      = srom->memory[k].type;
            }
        }
    }

    LOG_INFO("Core registered %u subsystem(s):", count);
    for (unsigned i = 0; i < count; ++i) {
        const struct subsystem_storage *ss = &fe->subsystem_info[i];
        LOG_INFO("  [%u] id=%u ident=%s desc=%s roms=%u",
                 i, ss->id,
                 ss->ident ? ss->ident : "(null)",
                 ss->desc  ? ss->desc  : "(null)",
                 ss->num_roms);
    }
    return true;
}

bool env_set_memory_maps(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_memory_maps: NULL data"); return false; }
    const struct retro_memory_map *map = (const struct retro_memory_map *)data;

    core_memory_maps_clear(fe);

    if (map->num_descriptors == 0 || !map->descriptors) {
        LOG_INFO("Core registered 0 memory descriptors");
        return true;
    }

    fe->memory_descriptors = calloc(map->num_descriptors,
        sizeof(*fe->memory_descriptors));
    fe->memory_addrspace_strings = calloc(map->num_descriptors,
        sizeof(*fe->memory_addrspace_strings));
    if (!fe->memory_descriptors ||
        !fe->memory_addrspace_strings) {
        core_memory_maps_clear(fe);
        return false;
    }

    for (unsigned i = 0; i < map->num_descriptors; ++i) {
        fe->memory_descriptors[i] = map->descriptors[i];
        if (map->descriptors[i].addrspace) {
            fe->memory_addrspace_strings[i] =
                SDL_strdup(map->descriptors[i].addrspace);
            fe->memory_descriptors[i].addrspace =
                fe->memory_addrspace_strings[i];
        } else {
            fe->memory_descriptors[i].addrspace = NULL;
        }
    }
    fe->memory_descriptor_count = map->num_descriptors;

    LOG_INFO("Core registered %u memory descriptor(s)",
             map->num_descriptors);
    return true;
}

bool env_set_content_info_override(struct frontend_state *fe, void *data)
{
    core_content_overrides_clear(fe);
    /* NULL data is a support probe per the libretro contract. */
    if (!data) {
        LOG_DEBUG("Core probed SET_CONTENT_INFO_OVERRIDE support");
        return true;
    }
    const struct retro_system_content_info_override *list =
        (const struct retro_system_content_info_override *)data;

    unsigned count = 0;
    for (const struct retro_system_content_info_override *p = list;
         p && p->extensions; ++p)
        count++;

    if (count == 0)
        return true;

    fe->content_overrides = calloc(count,
        sizeof(*fe->content_overrides));
    if (!fe->content_overrides)
        return false;
    fe->content_override_count = count;

    for (unsigned i = 0; i < count; ++i) {
        fe->content_overrides[i].extensions =
            SDL_strdup(list[i].extensions);
        fe->content_overrides[i].need_fullpath =
            list[i].need_fullpath;
        fe->content_overrides[i].persistent_data =
            list[i].persistent_data;
    }

    LOG_INFO("Core registered %u content info override(s); frontend keeps "
             "ROM data alive until shutdown regardless of persistent_data",
             count);
    return true;
}

