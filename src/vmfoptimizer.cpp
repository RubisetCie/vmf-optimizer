/*
 * Uses default Hammer/Hammer++ default values as an example. Custom FGDs not accounted.
 */

#include <algorithm>
#include <fstream>
#include <string>
#include <string_view>
#include <cstring>
#include <cstdlib>
#include <chrono>

#ifdef _WIN32
extern "C"
{
#include <io.h>
}
#define F_OK 0
#define access _access
#else
extern "C"
{
#include <linux/limits.h>
#include <unistd.h>
}
#endif

#define LOG(...) printf(__VA_ARGS__); if (savelog) fprintf(log, __VA_ARGS__);
#define VER(...) if (verbose) printf(__VA_ARGS__); if (savelog) fprintf(log, __VA_ARGS__);

using namespace std;

// Global variables
static bool verbose = false;
static bool carriages = false;
static bool prefab = false;
static bool process_solids = true;
static bool process_entities = true;
static bool remove_comment = false;
static bool remove_vplus = true;
static bool inplace = false;
static bool strip_ws = true;
static const char* savelog = nullptr;
static unsigned long long int count_i_all = 0, count_o_all = 0;
static FILE* log;

const string suffix("-optimized");

// The state of the current "chunk" in the VMF
enum State
{
    WORLD,
    SOLID,
    ENTITY,
    CLASS,
};

enum Type
{
    NONE,
    PROP_STATIC,
    PROP_DYNAMIC,
    PROP_OTHER,
    FUNC_BRUSH,
    FUNC_DETAIL,
    FUNC_DOOR,
    FUNC_AREAPORTAL,
    FUNC_AREAPORTALWINDOW,
    LIGHT,
    LIGHT_SPOT,
    LIGHT_DYNAMIC,
    LIGHT_ENVIRONMENT,
    INFO_DECAL,
    INFO_OVERLAY,
    INFO_PARTICLE_SYSTEM,
    ITEM_PACK,
    TRIGGER_ONCE,
    TRIGGER_MULTIPLE,
    TRIGGER_HURT,
    AMBIENT_GENERIC,
    POINT_SPOTLIGHT,
    BRUSH_ENTITIES,
    MOVE_ROPE,
    EDITOR
};

static string remove_whitespaces(const string_view line)
{
    string result;
    result.reserve(line.size());
    for (size_t i = 0; i < line.size();)
    {
        if (i + 2 < line.size() && line[i] == '"' && line[i+1] == ' ' && line[i+2] == '"')
        {
            result += "\"\"";
            i += 3;
        }
        else
        {
            const char c = line[i];
            if (c != '\r' && c != '\n' && c != '\t')
                result += c;
            i++;
        }
    }
    return result;
}

static bool inline contains(const string_view str, const string_view sub)
{
    return str.find(sub) != string::npos;
}

// Prints the command line usage
static void usage(const char* prg)
{
    printf("Usage: %s (options) (vmf-file) [(vmf-file-...)]\n  -h|--help         : Show help.\n  -i|--in-place     : Replace the original file.\n  -l|--log (file)   : Save the output to a separate log file.\n  -o|--output (dir) : Put all outputs to a specific directory.\n  -p|--prefab       : Erase editor-specific informations (intended for prefabs).\n  --skip-solids     : Skip solid processing.\n  --skip-defaults   : Skip removing entity parameters with default values.\n  --remove-comment  : Remove the map comment.\n  --keep-vert-plus  : Keep vertices plus informations (Hammer++ specific).\n  --keep-whitespace : Do not strip whitespaces to output lines (indentation, etc).\n  -c|-r|--carriages : Output with carriage returns in addition to line feeds.\n  -v|--verbose      : More verbose output.\n", prg);
}

static void optimize(ifstream& in, ofstream& out)
{
    unsigned int count_i = 0, count_o = 0;
    unsigned char gate = 0;
    bool isprefab = prefab;

    // Measure time taken
    auto timestart = chrono::high_resolution_clock::now();

    Type type = NONE;
    State state = WORLD;
    streampos pos;
    string line_base, line_low, line_raw;
    while (getline(in, line_base))
    {
        // Strip carriage returns to simplify comparisons
        line_base.erase(remove(line_base.begin(), line_base.end(), '\r'), line_base.end());

        // Skip empty lines
        if (line_base.empty())
            goto WRITE;

        // Precompute lowercase and whitespace-free version of the line for faster comparisons
        line_low = line_base;
        transform(line_low.begin(), line_low.end(), line_low.begin(), [](const unsigned char c) { return tolower(c); });
        line_raw = line_low;
        line_raw.erase(remove_if(line_raw.begin(), line_raw.end(), ::isspace), line_raw.end());

        // Branch on the state
        if (state == WORLD)
        {
            // Check if prefab
            if (!isprefab && contains(line_low, "prefab\" \"1"))
            {
                isprefab = true;
                VER("INFO:  File identified as a prefab\n");
            }

            // Remove the comment
            if (remove_comment && contains(line_low, "\"comment\""))
            {
                VER("INFO:  Found solid at line: %d\n", count_i + 1);
                goto NEXT;
            }

            // To the next state
            if (line_raw == "solid")
            {
                VER("INFO:  Found solid chunk at line: %d\n", count_i + 1);
                state = SOLID;
            }
        }
        else if (state == SOLID)
        {
            if (process_solids)
            {
                if (remove_vplus)
                {
                    if (gate == 0)
                    {
                        if (contains(line_low, "\"vertices_plus\""))
                        {
                            VER("INFO:  Removed vertices_plus at line: %d\n", count_i + 1);
                            gate = 1;
                            goto NEXT;
                        }
                    }
                    else
                    {
                        if (line_raw == "}")
                        {
                            if (--gate == 1) gate = 0;
                            goto NEXT;
                        }
                        else if (line_raw == "{")
                        {
                            gate++;
                            goto NEXT;
                        }
                    }
                }
                else
                {
                    // Clear zero values
                    if (contains(line_low, "\"smoothing_groups\" \"0\"") ||
                        contains(line_low, "\"rotation\" \"0\"") ||
                        contains(line_low, "\"elevation\" \"0\"") ||
                        contains(line_low, "\"subdiv\" \"0\""))
                        goto NEXT;
                }
            }

            // To the next state
            if (line_raw == "entity")
            {
                VER("INFO:  Found entity chunk at line: %d\n", count_i + 1);
                pos = in.tellg();
                state = ENTITY;
            }
        }
        else if (state == ENTITY)
        {
            if (process_entities)
            {
                // Find the classname of the entity
                if (contains(line_low, "classname"))
                {
                    if (contains(line_low, "\"prop_"))
                    {
                        if (contains(line_low, "static"))
                        {
                            VER("INFO:  Found prop_static at line: %d\n", count_i + 1);
                            type = PROP_STATIC;
                        }
                        else if (contains(line_low, "dynamic"))
                        {
                            VER("INFO:  Found prop_dynamic-like at line: %d\n", count_i + 1);
                            type = PROP_DYNAMIC; // Also affects prop_dynamic_override
                        }
                        else
                        {
                            VER("INFO:  Found prop at line: %d\n", count_i + 1);
                            type = PROP_OTHER;
                        }
                    }
                    else if (contains(line_low, "\"func_"))
                    {
                        if (contains(line_low, "detail"))
                        {
                            VER("INFO:  Found func_detail at line: %d\n", count_i + 1);
                            type = FUNC_DETAIL;
                        }
                        else if (contains(line_low, "brush"))
                        {
                            VER("INFO:  Found func_brush at line: %d\n", count_i + 1);
                            type = FUNC_BRUSH;
                        }
                        else if (contains(line_low, "door"))
                        {
                            VER("INFO:  Found func_door-like at line: %d\n", count_i + 1);
                            type = FUNC_DOOR; // Both normal and rotating
                        }
                        else if (contains(line_low, "areaportal"))
                        {
                            if (contains(line_low, "window\""))
                            {
                                VER("INFO:  Found func_areaportalwindow at line: %d\n", count_i + 1);
                                type = FUNC_AREAPORTALWINDOW;
                            }
                            else
                            {
                                VER("INFO:  Found func_areaportal at line: %d\n", count_i + 1);
                                type = FUNC_AREAPORTAL;
                            }
                        }
                        else type = NONE;
                    }
                    else if (contains(line_low, "\"light"))
                    {
                        if (contains(line_low, "spot"))
                        {
                            VER("INFO:  Found light_spot at line: %d\n", count_i + 1);
                            type = LIGHT_SPOT;
                        }
                        else if (contains(line_low, "dynamic"))
                        {
                            VER("INFO:  Found light_dynamic at line: %d\n", count_i + 1);
                            type = LIGHT_DYNAMIC;
                        }
                        else if (contains(line_low, "environment"))
                        {
                            VER("INFO:  Found light_environment at line: %d\n", count_i + 1);
                            type = LIGHT_ENVIRONMENT;
                        }
                        else
                        {
                            VER("INFO:  Found light at line: %d\n", count_i + 1);
                            type = LIGHT;
                        }
                    }
                    else if (contains(line_low, "\"info_"))
                    {
                        if (contains(line_low, "decal"))
                        {
                            VER("INFO:  Found info_decal at line: %d\n", count_i + 1);
                            type = INFO_DECAL;
                        }
                        else if (contains(line_low, "overlay"))
                        {
                            VER("INFO:  Found info_overlay at line: %d\n", count_i + 1);
                            type = INFO_OVERLAY;
                        }
                        else if (contains(line_low, "particle"))
                        {
                            VER("INFO:  Found info_particle_system at line: %d\n", count_i + 1);
                            type = INFO_PARTICLE_SYSTEM;
                        }
                        else type = NONE;
                    }
                    else if (contains(line_low, "\"trigger"))
                    {
                        if (contains(line_low, "multiple"))
                        {
                            VER("INFO:  Found trigger_multiple at line: %d\n", count_i + 1);
                            type = TRIGGER_MULTIPLE; // Multiple goes first because it's the most frequent
                        }
                        else if (contains(line_low, "hurt"))
                        {
                            VER("INFO:  Found trigger_hurt at line: %d\n", count_i + 1);
                            type = TRIGGER_HURT;
                        }
                        else
                        {
                            VER("INFO:  Found trigger at line: %d\n", count_i + 1);
                            type = TRIGGER_ONCE;
                        }
                    }
                    else if (contains(line_low, "\"ambient_generic"))
                    {
                        VER("INFO:  Found ambient_generic at line: %d\n", count_i + 1);
                        type = AMBIENT_GENERIC;
                    }
                    else if (contains(line_low, "\"item_"))
                    {
                        VER("INFO:  Found item at line: %d\n", count_i + 1);
                        type = ITEM_PACK;
                    }
                    else if (contains(line_low, "rope\""))
                    {
                        VER("INFO:  Found rope at line: %d\n", count_i + 1);
                        type = MOVE_ROPE;
                    }
                    else if (contains(line_low, "\"point_spotlight"))
                    {
                        VER("INFO:  Found point_spotlight at line: %d\n", count_i + 1);
                        type = POINT_SPOTLIGHT;
                    }
                    else if (contains(line_low, "\"editor"))
                    {
                        VER("INFO:  Found editor at line: %d\n", count_i + 1);
                        type = EDITOR;
                    }
                    else type = NONE;
                    state = CLASS;

                    // Rewind to the entity definition
                    in.seekg(pos);
                }
                else if (contains(line_low, "vertices_plus"))
                {
                    type = BRUSH_ENTITIES;
                    state = CLASS;

                    // Rewind to the entity definition
                    in.seekg(pos);
                }

                // Don't process the lines when checking classnames
                continue;
            }
        }
        else
        {
            // Clear default values
            switch (type)
            {
                case PROP_STATIC:
                    if (contains(line_low, "angles\" \"0 0 0") ||
                        contains(line_low, "fademaxdist\" \"0") ||
                        contains(line_low, "fademindist\" \"-1") ||
                        contains(line_low, "fadescale\" \"1") ||
                        contains(line_low, "lightmapresolutionx\" \"32") ||
                        contains(line_low, "lightmapresolutiony\" \"32") ||
                        contains(line_low, "skin\" \"0") ||
                        contains(line_low, "solid\" \"6"))
                        goto NEXT;
                    break;
                case PROP_DYNAMIC:
                    if (contains(line_low, "angles\" \"0 0 0") ||
                        contains(line_low, "disablebonefollowers\" \"0") ||
                        contains(line_low, "shadows\" \"0") ||
                        contains(line_low, "explodedamage\" \"0") ||
                        contains(line_low, "exploderadius\" \"0") ||
                        contains(line_low, "fademaxdist\" \"0") ||
                        contains(line_low, "fademindist\" \"-1") ||
                        contains(line_low, "fadescale\" \"1") ||
                        contains(line_low, "maxanimtime\" \"10") ||
                        contains(line_low, "minanimtime\" \"5") ||
                        contains(line_low, "modelscale\" \"1.0") ||
                        contains(line_low, "performancemode\" \"0") ||
                        contains(line_low, "pressuredelay\" \"0") ||
                        contains(line_low, "randomanimation\" \"0") ||
                        contains(line_low, "renderamt\" \"255") ||
                        contains(line_low, "rendercolor\" \"255 255 255") ||
                        contains(line_low, "renderfx\" \"0") ||
                        contains(line_low, "rendermode\" \"0") ||
                        contains(line_low, "setbodygroup\" \"0") ||
                        contains(line_low, "skin\" \"0") ||
                        contains(line_low, "solid\" \"6") ||
                        contains(line_low, "startdisabled\" \"0"))
                        goto NEXT;
                    break;
                case PROP_OTHER:
                    if (contains(line_low, "angles\" \"0 0 0") ||
                        contains(line_low, "damagetoenablemotion\" \"0") ||
                        contains(line_low, "damagetype\" \"0") ||
                        contains(line_low, "shadows\" \"0") ||
                        contains(line_low, "explodedamage\" \"0") ||
                        contains(line_low, "exploderadius\" \"0") ||
                        contains(line_low, "fademaxdist\" \"0") ||
                        contains(line_low, "fademindist\" \"-1") ||
                        contains(line_low, "fadescale\" \"1") ||
                        contains(line_low, "forcetoenablemotion\" \"0") ||
                        contains(line_low, "inertiascale\" \"1.0") ||
                        contains(line_low, "massscale\" \"0") ||
                        contains(line_low, "minhealthdmg\" \"0") ||
                        contains(line_low, "modelscale\" \"1.0") ||
                        contains(line_low, "nodamageforces\" \"0") ||
                        contains(line_low, "performancemode\" \"0") ||
                        contains(line_low, "physdamagescale\" \"0.1") ||
                        contains(line_low, "pressuredelay\" \"0") ||
                        contains(line_low, "renderamt\" \"255") ||
                        contains(line_low, "rendercolor\" \"255 255 255") ||
                        contains(line_low, "renderfx\" \"0") ||
                        contains(line_low, "rendermode\" \"0") ||
                        contains(line_low, "shadowcastdist\" \"0") ||
                        contains(line_low, "skin\" \"0"))
                        goto NEXT;
                    break;
                case FUNC_BRUSH:
                    if (contains(line_low, "inputfilter\" \"0") ||
                        contains(line_low, "invert_exclusion\" \"0") ||
                        contains(line_low, "renderamt\" \"255") ||
                        contains(line_low, "rendercolor\" \"255 255 255") ||
                        contains(line_low, "renderfx\" \"0") ||
                        contains(line_low, "rendermode\" \"0") ||
                        contains(line_low, "solidbsp\" \"0") ||
                        contains(line_low, "solidity\" \"0") ||
                        contains(line_low, "startdisabled"))
                        goto NEXT;
                    break;
                case FUNC_DETAIL:
                    if (contains(line_low, "dxlevel\" \"0"))
                        goto NEXT;
                    break;
                case FUNC_DOOR:
                    if (contains(line_low, "shadows\" \"0") ||
                        contains(line_low, "dmg\" \"0") ||
                        contains(line_low, "forceclosed\" \"0") ||
                        contains(line_low, "health\" \"0") ||
                        contains(line_low, "ignoredebris\" \"0") ||
                        contains(line_low, "lip\" \"0") ||
                        contains(line_low, "locked_sentence\" \"0") ||
                        contains(line_low, "loopmovesound\" \"0") ||
                        contains(line_low, "movedir\" \"0 0 0") ||
                        contains(line_low, "renderamt\" \"255") ||
                        contains(line_low, "rendercolor\" \"255 255 255") ||
                        contains(line_low, "renderfx\" \"0") ||
                        contains(line_low, "rendermode\" \"0") ||
                        contains(line_low, "speed\" \"100") ||
                        contains(line_low, "unlocked_sentence\" \"0"))
                        goto NEXT;
                    break;
                case FUNC_AREAPORTAL:
                    if (contains(line_low, "portalversion\" \"1") ||
                        contains(line_low, "startopen\" \"1"))
                        goto NEXT;
                    break;
                case FUNC_AREAPORTALWINDOW:
                    if (contains(line_low, "portalversion\" \"1") ||
                        contains(line_low, "translucencylimit\" \"0.2"))
                        goto NEXT;
                    break;
                case LIGHT_SPOT:
                    if (contains(line_low, "style\" \"0") ||
                        contains(line_low, "angles\" \"0 0 0") ||
                        contains(line_low, "_cone\" \"45") ||
                        contains(line_low, "_exponent\" \"1") ||
                        contains(line_low, "_inner_cone\" \"30") ||
                        contains(line_low, "pitch\" \"-90"))
                        goto NEXT;
                case LIGHT:
                    if (contains(line_low, "_constant_attn\" \"0") ||
                        contains(line_low, "_distance\" \"0") ||
                        contains(line_low, "_fifty_percent_distance\" \"0") ||
                        contains(line_low, "_hardfalloff\" \"0") ||
                        contains(line_low, "_light\" \"255 255 255 200") ||
                        contains(line_low, "_lighthdr\" \"-1 -1 -1 1") ||
                        contains(line_low, "_lightscalehdr\" \"1") ||
                        contains(line_low, "_linear_attn\" \"0") ||
                        contains(line_low, "_quadratic_attn\" \"1") ||
                        contains(line_low, "_zero_percent_distance\" \"0") ||
                        contains(line_low, "style\" \"0"))
                        goto NEXT;
                    break;
                case LIGHT_DYNAMIC:
                    if (contains(line_low, "style\" \"0") ||
                        contains(line_low, "_cone\" \"45") ||
                        contains(line_low, "_inner_cone\" \"30") ||
                        contains(line_low, "angles\" \"0 0 0") ||
                        contains(line_low, "brightness\" \"0") ||
                        contains(line_low, "distance\" \"120") ||
                        contains(line_low, "spotlight_radius\" \"80") ||
                        contains(line_low, "pitch\" \"-90"))
                        goto NEXT;
                    break;
                case LIGHT_ENVIRONMENT:
                    if (contains(line_low, "_ambient\" \"255 255 255 20") ||
                        contains(line_low, "_light\" \"255 255 255 200") ||
                        contains(line_low, "hdr\" \"-1 -1 -1 1") ||
                        contains(line_low, "calehdr\" \"1") ||
                        contains(line_low, "angles\" \"0 0 0") ||
                        contains(line_low, "_inner_cone\" \"30") ||
                        contains(line_low, "pitch\" \"0") ||
                        contains(line_low, "sunspreadangle\" \"0"))
                        goto NEXT;
                    break;
                case INFO_DECAL:
                    if (contains(line_low, "angles\" \"0 0 0"))
                        goto NEXT;
                    break;
                case INFO_OVERLAY:
                    if (contains(line_low, "fademaxdist\" \"0") ||
                        contains(line_low, "fademindist\" \"-1"))
                        goto NEXT;
                    break;
                case INFO_PARTICLE_SYSTEM:
                    if (contains(line_low, "angles\" \"0 0 0") ||
                        contains(line_low, "parent\" \"0") ||
                        contains(line_low, "flag_as_weather\" \"0") ||
                        contains(line_low, "start_active\" \"0"))
                        goto NEXT;
                    break;
                case TRIGGER_ONCE:
                    if (contains(line_low, "startdisabled\" \"0"))
                        goto NEXT;
                    break;
                case TRIGGER_MULTIPLE:
                    if (contains(line_low, "startdisabled\" \"0") ||
                        contains(line_low, "wait\" \"1"))
                        goto NEXT;
                    break;
                case TRIGGER_HURT:
                    if (contains(line_low, "startdisabled\" \"0") ||
                        contains(line_low, "damagemodel\" \"0") ||
                        contains(line_low, "damagetype\" \"0") ||
                        contains(line_low, "nodmgforce\" \"0"))
                        goto NEXT;
                    break;
                case AMBIENT_GENERIC:
                    if (contains(line_low, "cspinup\" \"0") ||
                        contains(line_low, "secs\" \"0") ||
                        contains(line_low, "health\" \"10") ||
                        contains(line_low, "lfomodpitch\" \"0") ||
                        contains(line_low, "lfomodvol\" \"0") ||
                        contains(line_low, "lforate\" \"0") ||
                        contains(line_low, "lfotype\" \"0") ||
                        contains(line_low, "pitch\" \"100") ||
                        contains(line_low, "pitchstart\" \"100") ||
                        contains(line_low, "preset\" \"0") ||
                        contains(line_low, "radius\" \"1250") ||
                        contains(line_low, "spindown\" \"0") ||
                        contains(line_low, "spinup\" \"0") ||
                        contains(line_low, "volstart\" \"0"))
                        goto NEXT;
                    break;
                case ITEM_PACK:
                    if (contains(line_low, "angles\" \"0 0 0") ||
                        contains(line_low, "automaterialize\" \"1") ||
                        contains(line_low, "fademaxdist\" \"0") ||
                        contains(line_low, "fademindist\" \"-1") ||
                        contains(line_low, "startdisabled\" \"0") ||
                        contains(line_low, "teamnum\" \"0"))
                        goto NEXT;
                    break;
                case MOVE_ROPE:
                    if (contains(line_low, "barbed\" \"0") ||
                        contains(line_low, "breakable\" \"0") ||
                        contains(line_low, "collide\" \"0") ||
                        contains(line_low, "dangling\" \"-1") ||
                        contains(line_low, "dxlevel\" \"0") ||
                        contains(line_low, "nowind\" \"0") ||
                        contains(line_low, "slack\" \"25") ||
                        contains(line_low, "subdiv\" \"2") ||
                        contains(line_low, "texturescale\" \"1") ||
                        contains(line_low, "type\" \"0") ||
                        contains(line_low, "width\" \"2") ||
                        line_base.find(R"(RopeMaterial" "cable/cable.vmt")") != string::npos)
                        goto NEXT;
                    break;
                case BRUSH_ENTITIES:
                    if (contains(line_low, "rotation\" \"0") ||
                        contains(line_low, "smoothing_groups\" \"0"))
                        goto NEXT;
                    break;
                case POINT_SPOTLIGHT:
                    if (contains(line_low, "angles\" \"0 0 0") ||
                        contains(line_low, "disablereceiveshadows\" \"0") ||
                        contains(line_low, "hdrcolorscale\" \"1.0") ||
                        contains(line_low, "ignoresolid\" \"0") ||
                        contains(line_low, "dxlevel\" \"0") ||
                        contains(line_low, "renderamt\" \"255") ||
                        contains(line_low, "rendercolor\" \"255 255 255") ||
                        contains(line_low, "renderfx\" \"0") ||
                        contains(line_low, "rendermode\" \"0") ||
                        contains(line_low, "spotlightlength\" \"500") ||
                        contains(line_low, "spotlightwidth\" \"50"))
                        goto NEXT;
                    break;
                case EDITOR:
                    if (contains(line_low, "angles\" \"-0 0 0") ||
                        contains(line_low, "color\" \"255 255 255") ||
                        contains(line_low, "textsize\" \"10"))
                        goto NEXT;
                    break;
            }

            if (remove_vplus)
            {
                if (gate == 0)
                {
                    if (contains(line_low, "\"vertices_plus\""))
                    {
                        VER("INFO:  Removed vertices_plus at line: %d\n", count_i + 1);
                        gate = 1;
                        goto NEXT;
                    }
                }
                else
                {
                    if (line_raw == "}")
                    {
                        if (--gate == 1) gate = 0;
                        goto NEXT;
                    }
                    else if (line_raw == "{")
                    {
                        gate++;
                        goto NEXT;
                    }
                }
            }

            // To the next state
            if (line_raw == "entity")
            {
                VER("INFO:  Found entity chunk at line: %d\n", count_i + 1);
                pos = in.tellg();
                state = ENTITY;
            }
        }

        // Clear some editor infos useless for prefabs
        if (isprefab)
        {
            if (gate == 0)
            {
                if (line_raw == "editor" ||
                    line_raw == "viewsettings" ||
                    line_raw == "visgroups" ||
                    line_raw == "cameras" ||
                    line_raw == "cordons")
                {
                    gate = 1;
                    goto NEXT;
                }
            }
            else
            {
                if (line_raw == "}")
                {
                    if (--gate == 1) gate = 0;
                    goto NEXT;
                }
                else if (line_raw == "{")
                {
                    gate++;
                    goto NEXT;
                }
            }
        }

      WRITE:
        // Gate control
        if (gate > 0)
            goto NEXT;

        // Write the line (stripped)
        if (strip_ws) out << remove_whitespaces(line_base);
        else out << line_base;
        if (!carriages) out << "\n";
        else out << "\r\n";
        count_o++;
      NEXT:
        count_i++;
    }

    // Compute the elapsed time
    auto timeend = chrono::high_resolution_clock::now();
    auto milliseconds = chrono::duration_cast<chrono::milliseconds>(timeend - timestart);

    const unsigned char ratio = ((static_cast<float>(count_o) / count_i) * 100.0f);
    LOG("  Done! (Time: %lu ms - Ratio: %u%%)\n", milliseconds.count(), ratio);

    // Add to the global counters
    count_i_all += count_i;
    count_o_all += count_o;
}

int main(int argc, const char** argv)
{
    const char* outdir = nullptr;
    size_t start;

    // Checks the options
    for (start = 1; start < argc; start++)
    {
        const char* arg = argv[start];

        if (arg[0] == '-')
        {
            // Single letter arguments
            if (arg[1] != '-')
            {
                for (size_t i = 1; arg[i] != '\0'; i++)
                {
                    switch (arg[i])
                    {
                        case '?':
                        case 'h':
                            usage(argv[0]);
                            return 0;
                        case 'i':
                            inplace = true;
                            break;
                        case 'l':
                            if (start + 1 >= argc)
                            {
                                fprintf(stderr, "Need to supply a log file after -l,--log!\n");
                                return 2;
                            }
                            savelog = argv[++start];
                            break;
                        case 'o':
                            if (start + 1 >= argc)
                            {
                                fprintf(stderr, "Need to supply a directory after -o,--output!\n");
                                return 2;
                            }
                            outdir = argv[++start];
                            break;
                        case 'p':
                            prefab = true;
                            break;
                        case 'c':
                        case 'r':
                            carriages = true;
                            break;
                        case 'v':
                            verbose = true;
                            break;
                        default:
                            fprintf(stderr, "Unrecognized option: %s\n", arg);
                            usage(argv[0]);
                            return 2;
                    }
                }
            }
            // Long words arguments
            else
            {
                if (strcmp(arg, "--help") == 0)
                {
                    usage(argv[0]);
                    return 0;
                }
                else if (strcmp(arg, "--verbose") == 0)
                    verbose = true;
                else if (strcmp(arg, "--in-place") == 0 || strcmp(arg, "--inplace") == 0)
                    inplace = true;
                else if (strcmp(arg, "--log") == 0)
                {
                    if (start + 1 >= argc)
                    {
                        fprintf(stderr, "Need to supply a log file after -l,--log!\n");
                        return 2;
                    }
                    savelog = argv[++start];
                }
                else if (strcmp(arg, "--output") == 0)
                {
                    if (start + 1 >= argc)
                    {
                        fprintf(stderr, "Need to supply a directory after -o,--output!\n");
                        return 2;
                    }
                    outdir = argv[++start];
                }
                else if (strcmp(arg, "--prefab") == 0)
                    prefab = true;
                else if (strcmp(arg, "--skip-solids") == 0)
                    process_solids = false;
                else if (strcmp(arg, "--skip-defaults") == 0)
                    process_entities = false;
                else if (strcmp(arg, "--remove-comment") == 0)
                    remove_comment = true;
                else if (strcmp(arg, "--keep-vert-plus") == 0)
                    remove_vplus = false;
                else if (strcmp(arg, "--keep-whitespace") == 0)
                    strip_ws = false;
                else if (strcmp(arg, "--carriages") == 0)
                    carriages = true;
                else if (strcmp(arg, "--") == 0)
                    break;
                else
                {
                    fprintf(stderr, "Unrecognized option: %s\n", arg);
                    usage(argv[0]);
                    return 2;
                }
            }
        }
        // Stop parsing arguments
        else
            break;
    }

    // Create the log file
    if (savelog)
    {
        log = fopen(savelog, "ab");
        if (!log)
        {
            fprintf(stderr, "Failed to open/create log file: %s\n", savelog);
            savelog = nullptr;
        }
    }

    if (start >= argc)
    {
        usage(argv[0]);
        return 0;
    }

    // Create the output directory if doesn't exists
    if (outdir)
    {
        if (access(outdir, W_OK) != 0)
        {
            fprintf(stderr, "Cannot access to output directory: %s\n", outdir);
            return 1;
        }
    }

    // Process each input file
    unsigned short int filecount = 0;
    for (; start < argc; start++)
    {
        // Open the files
        ifstream in;
        ofstream out;
        in.open(argv[start], ifstream::binary);
        if (!in)
        {
            LOG("ERROR: Could not open file: %s\n", argv[start]);
            continue;
        }

        // Create the temporary file
#ifdef _WIN32
        char temp_name[PATH_MAX];
        const char* temp = getenv("TEMP");
        const size_t temp_len = strlen(temp);
        memcpy(temp_name, temp, temp_len);
        memcpy(&temp_name[temp_len], "\\.vmfoXXXXXX", temp_len);
        if (!_mktemp(temp_name))
#else
        char temp_name[PATH_MAX] = "/tmp/.vmfoXXXXXX";
        if (!mkstemp(temp_name))
#endif
        {
            LOG("ERROR: Could not create temporary file!\n");
            goto CLOSE_IN;
        }
        out.open(temp_name, ofstream::binary);
        if (!out)
        {
            LOG("ERROR: Could not open temporary file: %s\n", temp_name);
            goto CLOSE_IN;
        }

        LOG("INFO:  Compressing: %s...", argv[start]);
        optimize(in, out);
        filecount++;

        // Close the files
        out.close();
      CLOSE_IN:
        in.close();

        // Move the output file
        if (inplace)
        {
            if (remove(argv[start]) != 0 && rename(temp_name, argv[start]) != 0)
            {
                LOG("ERROR: Failed to replace the file: %s\n", argv[start]);
                continue;
            }
        }
        else
        {
            const string filename(argv[start]);
            string new_name;
            if (outdir)
            {
                new_name = outdir;
                new_name.append(filename.substr(filename.find_last_of("/\\") + 1));
            }
            else
            {
                new_name = filename;
                new_name.erase(new_name.length() - 4).append(suffix).append(".vmf");
            }
            if (access(new_name.c_str(), F_OK) == 0)
                remove(new_name.c_str());
            if (rename(temp_name, new_name.c_str()) != 0)
            {
                LOG("ERROR: Failed to rename the file: %s\n", argv[start]);
                continue;
            }
        }
    }

    if (filecount == 0)
        return 1;

    const unsigned char ratio = ((static_cast<float>(count_o_all) / count_i_all) * 100.0f);
    LOG("Successful compression of %u files!\n  Global ratio: %u%%\n", filecount, ratio);
    return 0;
}
