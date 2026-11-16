#include "mako_status.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace vkBasalt;
namespace fs = std::filesystem;

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

static std::string profileText(const std::string& model, bool scaling)
{
    return std::string("{\"name\":\"vkdaedalus \\\"default\\\"\",\"gpu\":null,\"multiplier\":2,\"gamescope_vrr_mode\":\"follow-steam\",")
           + "\"frame_generation_provisioned\":true,\"frame_generation_model\":\"" + model + "\",\"frame_generation_enabled\":false,\"scaling_enabled\":"
           + (scaling ? "true" : "false") + ",\"scaling_method\":\"mako\",\"scaling_factor\":1.5,\"scaling_sharpness\":0.800000012,\"pacing\":\"none\"}";
}

static std::string statusText(const std::string& role, int context, const std::string& appliedModel, bool appliedScaling, bool active, const std::string& reason,
                              const std::string& sizes, const std::string& method)
{
    return "{\"schema_version\":5,\"pid\":4242,\"process_start_ticks\":77,\"context\":" + std::to_string(context) + ",\"role\":\"" + role
           + "\",\"updated_unix_ms\":1794503850123,\"state_revision\":3,\"phase\":\"active\",\"reason\":\"configuration\",\"pending\":{\"frame_generation_private\":false,"
             "\"spatial_private\":false,\"swapchain_recreation\":false,\"process_restart\":false},\"applied_generated_capacity\":0,\"frame_generation_active\":false,"
             "\"frame_generation_unavailable\":{\"reason\":\"offscreen-x11-window\"},\"gpu_headroom_state\":\"off\",\"spatial_scaling\":{\"active\":"
           + (active ? "true" : "false") + ",\"activation_supported\":true,\"inactive_reason\":" + (reason.empty() ? "null" : "\"" + reason + "\"")
           + ",\"constraint_reason\":null," + sizes + ",\"gamescope_target_width\":0,\"gamescope_target_height\":0,\"requested_method\":\"mako\",\"active_method\":\""
           + method + "\",\"effective_factor\":1.5,\"pipeline\":\"pre-frame-generation\",\"supersampling_active\":false,\"fallback_reason\":null,"
             "\"non_supersampling_factor_ceiling\":null},\"requested\":"
           + profileText("lossless-scaling", true) + ",\"applied\":" + profileText(appliedModel, appliedScaling) + ",\"error\":null}\n";
}

static std::string sizesText(int sourceWidth, int sourceHeight, int presentationWidth, int presentationHeight)
{
    return "\"source_width\":" + std::to_string(sourceWidth) + ",\"source_height\":" + std::to_string(sourceHeight) + ",\"presentation_width\":"
           + std::to_string(presentationWidth) + ",\"presentation_height\":" + std::to_string(presentationHeight);
}

static void write(const fs::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: mako-status-test <work folder>\n");
        return 2;
    }
    const fs::path work = argv[1];
    fs::remove_all(work);
    fs::create_directories(work / "runtime-state");

    const std::string scalingText = statusText("frame-generation", 1, "mako-fg", true, true, "", sizesText(853, 480, 1280, 720), "mako");
    std::string       problem;
    const std::optional<MakoStatus> scaling = parseMakoStatus(scalingText, problem);
    expect(scaling && problem.empty(), "a status file as MAKO writes it is read: " + problem);
    if (scaling)
    {
        expect(scaling->role == "frame-generation" && scaling->context == 1, "its role and its swapchain's context are read");
        expect(scaling->appliedModel == "mako-fg" && scaling->appliedScalingEnabled, "the model and the Scaling switch are read from the applied profile, not the requested one");
        expect(scaling->scalingActive && !scaling->scalingInactiveReason && scaling->scalingMethod == "mako", "active scaling has no inactive reason and names its method");
        expect(scaling->sourceWidth == 853 && scaling->sourceHeight == 480 && scaling->presentationWidth == 1280 && scaling->presentationHeight == 720,
               "the source and presentation sizes are read");
        expect(makoScalingSizes(*scaling) == "853x480 to 1280x720", "the sizes are shown as source to presentation");
    }
    const std::string nativeText =
        statusText("frame-generation", 2, "lossless-scaling", true, false, "application-extent-mismatch", sizesText(1280, 720, 1280, 720), "native");
    const std::optional<MakoStatus> native = parseMakoStatus(nativeText, problem);
    expect(native && !native->scalingActive && native->scalingInactiveReason == "application-extent-mismatch", "inactive scaling carries MAKO's reason");

    for (const std::string& broken : {std::string(), std::string("[]"), std::string("{\"role\":\"frame-generation\""), scalingText + "x",
                                      statusText("frame-generation", 1, "mako-fg", true, true, "", "\"source_width\":853", "mako"),
                                      statusText("frame-generation", -1, "mako-fg", true, true, "", sizesText(853, 480, 1280, 720), "mako"),
                                      statusText("frame-generation", 1, "mako-fg", true, true, "", sizesText(853, 480, 1280, -720), "mako")})
    {
        problem.clear();
        expect(!parseMakoStatus(broken, problem) && !problem.empty(), "a file that is no whole status is refused with a reason: " + broken.substr(0, 40));
    }
    std::string deep = "1";
    for (int i = 0; i < 64; i++)
        deep = "{\"a\":" + deep + "}";
    expect(!parseMakoStatus(deep, problem), "a file nested deeper than any status is refused");

    expect(makoStatusFolder("/home/a/.config/mako-render/conf.toml") == "/home/a/.config/mako-render/runtime-state" && makoStatusFolder("/named/mako.toml") == "/named/runtime-state",
           "MAKO's status folder is runtime-state beside its file");
    expect(isMakoStatusFileOf("4242-77-frame-generation-1.json", 4242, 77) && isMakoStatusFileOf("4242-77-spatial-scaling-3.json", 4242, 77),
           "a status file of this process is its own, for either of MAKO's roles");
    expect(!isMakoStatusFileOf("4242-78-frame-generation-1.json", 4242, 77) && !isMakoStatusFileOf("14242-77-frame-generation-1.json", 4242, 77)
               && !isMakoStatusFileOf("4242-77-frame-generation-1.json.lock", 4242, 77) && !isMakoStatusFileOf("4242-77-frame-generation-1.json.tmp", 4242, 77),
           "another process's file, a process id reused with another start time, a lock and a half-written file are not");

    if (scaling && native)
    {
        expect(makoStatusIsOfSwapchain(*scaling, 853, 480) && makoStatusIsOfSwapchain(*scaling, 1280, 720) && !makoStatusIsOfSwapchain(*scaling, 1920, 1080)
                   && !makoStatusIsOfSwapchain(*scaling, 853, 720),
               "a status is this swapchain's when the swapchain has its source size, above MAKO, or its presentation size, below MAKO");

        MakoStatus older       = *native;
        older.context          = 1;
        MakoStatus lowerRole   = *scaling;
        lowerRole.role         = "spatial-scaling";
        lowerRole.context      = 1;
        lowerRole.appliedModel = "lossless-scaling";
        MakoStatus upperRole   = *native;
        upperRole.context      = 9;
        upperRole.appliedModel = "mako-fg";
        upperRole.scalingInactiveReason.reset();
        expect(makoScalingStatus({}) == nullptr && makoGenerationStatus({}) == nullptr, "no status file gives no status");
        expect(makoScalingStatus({older, *native})->context == 2 && makoGenerationStatus({*native, older})->context == 2,
               "of two swapchains of the same size the newer one's status is taken");
        expect(makoScalingStatus({upperRole, lowerRole})->role == "spatial-scaling" && makoGenerationStatus({upperRole, lowerRole})->role == "frame-generation",
               "with MAKO split in two roles the scaling comes from the role that scales and the model from the one that generates");
        expect(makoScalingStatus({upperRole, older})->context == 1, "of two inactive ones the one that names a reason is taken");

        expect(makoFellBackToMakoFg("lossless-scaling", {*scaling}), "MAKO started for the Lossless Scaling model and running MAKO FG fell back");
        expect(!makoFellBackToMakoFg("lossless-scaling", {*native}) && !makoFellBackToMakoFg("mako-fg", {*scaling}) && !makoFellBackToMakoFg("lossless-scaling", {})
                   && !makoFellBackToMakoFg("lossless-scaling", {lowerRole}),
               "running the model it started for, started for MAKO FG, with no status, or with only the scaling role's status, it did not");

        const MakoStatusRead none;
        const MakoStatusRead unreadable{{}, "MAKO's status file is not a JSON object"};
        const MakoStatusRead active{{*scaling}, ""};
        const MakoStatusRead inactive{{*native}, ""};
        MakoStatus           startedOff     = *native;
        startedOff.appliedScalingEnabled    = false;
        startedOff.scalingInactiveReason.reset();
        MakoStatus unexplained = *native;
        unexplained.scalingInactiveReason.reset();
        expect(makoScalingShown(false, false, active) == MakoScalingShown{MakoScalingRun::Off, "", ""}
                   && makoScalingShown(true, false, active) == MakoScalingShown{MakoScalingRun::NotStarted, "", ""},
               "while MAKO does not run the profile, Scaling is off or waits for the start, whatever a status file says");
        expect(makoScalingShown(true, true, active) == MakoScalingShown{MakoScalingRun::Scaling, "853x480 to 1280x720", "mako"}
                   && makoScalingShown(false, true, active).run == MakoScalingRun::Scaling,
               "scaling is on only when MAKO reports it active, with its sizes and method, and stays on in this run after the switch is turned off");
        expect(makoScalingShown(true, true, inactive)
                   == MakoScalingShown{MakoScalingRun::NotScaling, "MAKO does not scale: the game asks for a size MAKO did not offer it", ""},
               "switched on and reported inactive, it shows MAKO's reason");
        expect(makoScalingShown(false, true, inactive).run == MakoScalingRun::Off && makoScalingShown(false, true, none).run == MakoScalingRun::Off,
               "switched off and not reported active, it is off");
        expect(makoScalingShown(true, true, none)
                   == MakoScalingShown{MakoScalingRun::NoStatus, "MAKO has written no status file for this swapchain, so what it scales is not known", ""},
               "switched on with no status file, it says there is none and claims nothing");
        expect(makoScalingShown(true, true, unreadable) == MakoScalingShown{MakoScalingRun::NoStatus, "MAKO's status file is not a JSON object", ""},
               "a status file that cannot be read is named as that");
        expect(makoScalingShown(true, true, MakoStatusRead{{startedOff}, ""}).run == MakoScalingRun::NotStarted,
               "switched on after MAKO started without it, it waits for the next start");
        expect(makoScalingShown(true, true, MakoStatusRead{{unexplained}, ""})
                   == MakoScalingShown{MakoScalingRun::NotScaling, "MAKO does not scale, and names no reason", ""},
               "inactive with no reason from MAKO, it says so");
    }
    expect(makoScalingInactiveWords("factor-not-upscaling") == "MAKO does not scale: the factor is 1x, which scales nothing"
               && makoScalingInactiveWords("application-extent-override-no-source-presentation-split")
                      == "MAKO does not scale: the game asks for the window's full size, not the smaller size MAKO offers it"
               && makoScalingInactiveWords("swapchain-format-unsupported") == "MAKO does not scale, and names the reason swapchain-format-unsupported",
           "a reason this card has words for is said in them, and any other is named as MAKO names it");

    const std::string folder = (work / "runtime-state").string();
    expect(readMakoStatus((work / "missing").string(), 4242, 77, 1280, 720).ofSwapchain.empty() && readMakoStatus((work / "missing").string(), 4242, 77, 1280, 720).problem.empty(),
           "a status folder MAKO has not made yet is no status and no problem");
    write(work / "runtime-state" / "4242-77-frame-generation-1.json", scalingText);
    write(work / "runtime-state" / "4242-77-frame-generation-1.json.lock", "");
    write(work / "runtime-state" / "4242-77-frame-generation-2.json", statusText("frame-generation", 2, "mako-fg", true, true, "", sizesText(640, 360, 1920, 1080), "mako"));
    write(work / "runtime-state" / "4243-77-frame-generation-7.json", nativeText);
    const MakoStatusRead mine = readMakoStatus(folder, 4242, 77, 1280, 720);
    expect(mine.ofSwapchain.size() == 1 && mine.ofSwapchain[0].context == 1 && mine.problem.empty(),
           "of this process's status files the one of this swapchain's size is read, and no other process's");
    expect(readMakoStatus(folder, 4242, 77, 800, 600).ofSwapchain.empty() && readMakoStatus(folder, 4242, 77, 800, 600).problem.empty(),
           "a swapchain none of them names has no status");
    expect(readMakoStatus(folder, 4243, 77, 1280, 720).ofSwapchain.size() == 1 && readMakoStatus(folder, 4242, 78, 1280, 720).ofSwapchain.empty(),
           "each process reads its own, by process id and start time");
    write(work / "runtime-state" / "4244-77-frame-generation-1.json", "{\"role\":");
    expect(readMakoStatus(folder, 4244, 77, 1280, 720).ofSwapchain.empty() && readMakoStatus(folder, 4244, 77, 1280, 720).problem == "MAKO's status file is not a JSON object",
           "a status file that is cut short is a problem with its reason, not a status");
    write(work / "runtime-state" / "4244-77-frame-generation-2.json", scalingText);
    expect(readMakoStatus(folder, 4244, 77, 1280, 720).ofSwapchain.size() == 1 && readMakoStatus(folder, 4244, 77, 1280, 720).problem.empty(),
           "and is no problem once another file of the process gives this swapchain's status");
    expect(thisProcessStartTicks() != 0, "this process's start time is read from /proc, as MAKO reads it to name its status file");

    if (failures != 0)
    {
        std::printf("%d mako-status check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
