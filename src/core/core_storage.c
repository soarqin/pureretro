/*
 * PureRetro — Core-owned frontend storage cleanup
 */

#include "core_internal.h"

#include <stdlib.h>
#include <string.h>

void core_controller_ports_clear(struct frontend_state *fe)
{
    for (unsigned p = 0; p < fe->controller_port_count; ++p) {
        struct controller_port_info *port = &fe->controller_ports[p];
        for (unsigned i = 0; i < port->num_types; ++i)
            free((char *)port->types[i].desc);
        free(port->types);
        port->types = NULL;
        port->num_types = 0;
    }
    fe->controller_port_count = 0;
}

void core_subsystem_info_clear(struct frontend_state *fe)
{
    for (unsigned i = 0; i < fe->subsystem_info_count; ++i) {
        struct subsystem_storage *ss = &fe->subsystem_info[i];
        free(ss->desc);
        free(ss->ident);
        for (unsigned j = 0; j < ss->num_roms; ++j) {
            struct subsystem_rom_storage *rs = &ss->roms[j];
            free(rs->desc);
            free(rs->valid_extensions);
            for (unsigned k = 0; k < rs->num_memory; ++k)
                free(rs->memory_extensions[k]);
            free(rs->memory_extensions);
            free(rs->memory);
        }
        free(ss->roms);
    }
    free(fe->subsystem_info);
    fe->subsystem_info = NULL;
    fe->subsystem_info_count = 0;
}

void core_memory_maps_clear(struct frontend_state *fe)
{
    for (unsigned i = 0; i < fe->memory_descriptor_count; ++i)
        free(fe->memory_addrspace_strings[i]);
    free(fe->memory_addrspace_strings);
    free(fe->memory_descriptors);
    fe->memory_descriptors = NULL;
    fe->memory_addrspace_strings = NULL;
    fe->memory_descriptor_count = 0;
}

void core_content_overrides_clear(struct frontend_state *fe)
{
    for (unsigned i = 0; i < fe->content_override_count; ++i)
        free(fe->content_overrides[i].extensions);
    free(fe->content_overrides);
    fe->content_overrides = NULL;
    fe->content_override_count = 0;
}

