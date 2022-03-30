#include <application.h>
#include <imgui.h>
#include <imgui_helper.h>
#include <imgui_extra_widget.h>
#include <imgui_json.h>
#include <ImGuiFileDialog.h>
#include <ImGuiTabWindow.h>
#if IMGUI_VULKAN_SHADER
#include <Histogram_vulkan.h>
#include <Waveform_vulkan.h>
#include <CIE_vulkan.h>
#include <Vector_vulkan.h>
#endif
#include "MediaTimeline.h"
#include "FFUtils.h"
#include "Logger.h"
#include <sstream>

#define DEFAULT_MAIN_VIEW_WIDTH     1680
#define DEFAULT_MAIN_VIEW_HEIGHT    1024

using namespace MediaTimeline;

static const char* color_system_items[] = { "NTSC", "EBU", "SMPTE", "SMPTE 240M", "APPLE", "wRGB", "CIE1931", "Rec709", "Rec2020", "DCIP3" };
static const char* cie_system_items[] = { "XYY", "UCS", "LUV" };

static const char* ConfigureTabNames[] = {
    "System",
    "Timeline"
};

static const char* ControlPanelTabNames[] = {
    ICON_MEDIA_BANK " Meida",
    ICON_MEDIA_TRANS " Transition",
    ICON_MEDIA_FILTERS " Filters",
    ICON_MEDIA_OUTPUT " Output"
};

static const char* ControlPanelTabTooltips[] = 
{
    "Meida Bank",
    "Transition Bank",
    "Filters Bank",
    "Meida Output"
};

static const char* MainWindowTabNames[] = {
    ICON_MEDIA_PREVIEW " Preview",
    ICON_MEDIA_VIDEO " Video",
    ICON_MUSIC " Audio",
};

static const char* MainWindowTabTooltips[] = 
{
    "Meida Preview",
    "Video Editor",
    "Audio Editor",
};

static const char* ScopeWindowTabNames[] = {
    ICON_HISTOGRAM " Video Histogram",
    ICON_WAVEFORM " Video Waveform",
    ICON_CIE " CIE",
    ICON_VETCTOR " Video Vector",
    ICON_DB_LEVEL " Audio dB Level",
    ICON_SPECTROGRAM " Audio Spectrogram"
};

static const char* VideoEditorTabNames[] = {
    ICON_BLUE_PRINT,
    ICON_TRANS,
    ICON_CROP,
    ICON_ROTATE
};

static const char* VideoEditorTabTooltips[] = {
    "Video Filter",
    "Video Fusion",
    "Video Crop",
    "Video Rotate"
};

static const char* AudioEditorTabNames[] = {
    ICON_BLUE_PRINT,
    ICON_TRANS,
};

static const char* AudioEditorTabTooltips[] = {
    "Audio Filter",
    "Audio Fusion",
};

struct MediaEditorSettings
{
    float TopViewHeight {0.6};              // Top view height percentage
    float BottomViewHeight {0.4};           // Bottom view height percentage
    float ControlPanelWidth {0.3};          // Control panel view width percentage
    float MainViewWidth {0.7};              // Main view width percentage
    bool BottomViewExpanded {true};         // Timeline/Scope view expended
    float OldBottomViewHeight {0.4};        // Old Bottom view height, recorde at non-expended
    //float 
    int VideoWidth  {1920};                 // timeline Media Width
    int VideoHeight {1080};                 // timeline Media Height
    MediaInfo::Ratio VideoFrameRate {25000, 1000};// timeline frame rate
    MediaInfo::Ratio PixelAspectRatio {1, 1}; // timeline pixel aspect ratio
    int VideoFrameCacheSize {10};           // timeline video cache size
    int AudioChannels {2};                  // timeline audio channels
    int AudioSampleRate {44100};            // timeline audio sample rate
    int AudioFormat {2};                    // timeline audio format 0=unknown 1=s16 2=f32
    std::string project_path;               // Editor Recently project file path
    int BankViewStyle {0};                  // Bank view style type, 0 = icons, 1 = tree vide, and ... 
    bool ShowHelpTooltips {false};          // Show UI help tool tips

    // Histogram Scope tools
    bool HistogramLog {false};
    float HistogramScale {0.1};

    // Waveform Scope tools
    bool WaveformMirror {true};
    bool WaveformSeparate {false};
    float WaveformIntensity {2.0};

    // CIE Scope tools
    int CIEColorSystem {ImGui::Rec709system};
    int CIEMode {ImGui::XYY};
    int CIEGamuts {ImGui::Rec2020system};
    float CIEContrast {0.75};
    float CIEIntensity {0.5};
    bool CIECorrectGamma {false};
    bool CIEShowColor {true};

    // Vector Scope tools
    float VectorIntensity {0.5};

    MediaEditorSettings() {}
};

static std::string ini_file = "Media_Editor.ini";
static TimeLine * timeline = nullptr;
static ImGui::TabLabelStyle * tab_style = &ImGui::TabLabelStyle::Get();
static MediaEditorSettings g_media_editor_settings;
static MediaEditorSettings g_new_setting;
static imgui_json::value g_project;
static bool quit_save_confirm = true;

static int ConfigureIndex = 0;              // default timeline setting
static int ControlPanelIndex = 0;           // default Media Bank window
static int MainWindowIndex = 0;             // default Media Preview window
static int BottomWindowIndex = 0;           // default Media Timeline window, no other view so far
static int VideoEditorWindowIndex = 0;      // default Video Filter window
static int AudioEditorWindowIndex = 0;      // default Audio Filter window
static int LastMainWindowIndex = 0;
static int LastVideoEditorWindowIndex = 0;
static int LastAudioEditorWindowIndex = 0;
static int ScopeWindowIndex = 0;            // default Histogram Scope window 

static int MonitorIndexPreviewVideo = -1;
static int MonitorIndexVideoFilterOrg = -1;
static int MonitorIndexVideoFiltered = -1;

static float ui_breathing = 1.0f;
static float ui_breathing_step = 0.01;
static float ui_breathing_min = 0.5;
static float ui_breathing_max = 1.0;

#if IMGUI_VULKAN_SHADER
static ImGui::Histogram_vulkan *    m_histogram {nullptr};
static ImGui::Waveform_vulkan *     m_waveform {nullptr};
static ImGui::CIE_vulkan *          m_cie {nullptr};
static ImGui::Vector_vulkan *       m_vector {nullptr};
#endif

static ImGui::ImMat mat_histogram;

static ImGui::ImMat mat_waveform;
static ImTextureID waveform_texture {nullptr};

static ImGui::ImMat mat_cie;
static ImTextureID cie_texture {nullptr};

static ImGui::ImMat mat_vector;
static ImTextureID vector_texture {nullptr};

static void UpdateBreathing()
{
    ui_breathing -= ui_breathing_step;
    if (ui_breathing <= ui_breathing_min)
    {
        ui_breathing = ui_breathing_min;
        ui_breathing_step = -ui_breathing_step;
    }
    else if (ui_breathing >= ui_breathing_max)
    {
        ui_breathing = ui_breathing_max;
        ui_breathing_step = -ui_breathing_step;
    }
}
static bool UIPageChanged()
{
    bool updated = false;
    if (LastMainWindowIndex == 0 && MainWindowIndex != 0)
    {
        // we leave video preview windows, stop preview play
        Logger::Log(Logger::DEBUG) << "[Changed page] leaving video preview page!!!" << std::endl;
        if (timeline)
            timeline->Play(false);
    }
    if (LastMainWindowIndex == 1 && LastVideoEditorWindowIndex == 0 && (
        MainWindowIndex != 1 || VideoEditorWindowIndex != 0))
    {
        // we leave video filter windows, stop filter play, check unsaved bp
        Logger::Log(Logger::DEBUG) << "[Changed page] leaving video filter page!!!" << std::endl;
        if (timeline && timeline->mVidFilterClip)
        {
            timeline->mVidFilterClipLock.lock();
            timeline->mVidFilterClip->bPlay = false;
            timeline->mVidFilterClip->Save();
            timeline->mVidFilterClipLock.unlock();
            updated = true;
        }
    }
    if (LastMainWindowIndex == 1 && LastVideoEditorWindowIndex == 1 && (
        MainWindowIndex != 1 || VideoEditorWindowIndex != 1))
    {
        // we leave video fusion windows, stop fusion play, check unsaved bp
        Logger::Log(Logger::DEBUG) << "[Changed page] leaving video fusion page!!!" << std::endl;
    }
    if (LastMainWindowIndex == 1 && LastVideoEditorWindowIndex == 2 && (
        MainWindowIndex != 1 || VideoEditorWindowIndex != 2))
    {
        // we leave video crop windows
        Logger::Log(Logger::DEBUG) << "[Changed page] leaving video crop page!!!" << std::endl;
    }
    if (LastMainWindowIndex == 1 && LastVideoEditorWindowIndex == 3 && (
        MainWindowIndex != 1 || VideoEditorWindowIndex != 3))
    {
        // we leave video rotate windows
        Logger::Log(Logger::DEBUG) << "[Changed page] leaving video rotate page!!!" << std::endl;
    }
    if (LastMainWindowIndex == 2 && LastAudioEditorWindowIndex == 0 && (
        MainWindowIndex != 2 || AudioEditorWindowIndex != 0))
    {
        // we leave audio filter windows, stop filter play, check unsaved bp
        Logger::Log(Logger::DEBUG) << "[Changed page] leaving audio filter page!!!" << std::endl;
        if (timeline && timeline->mAudFilterClip)
        {
            timeline->mAudFilterClipLock.lock();
            timeline->mAudFilterClip->bPlay = false;
            timeline->mAudFilterClip->Save();
            timeline->mAudFilterClipLock.unlock();
        }
        updated = true;
    }
    if (LastMainWindowIndex == 2 && LastAudioEditorWindowIndex == 1 && (
        MainWindowIndex != 2 || AudioEditorWindowIndex != 1))
    {
        // we leave audio fusion windows, stop fusion play, check unsaved bp
        Logger::Log(Logger::DEBUG) << "[Changed page] leaving audio fusion page!!!" << std::endl;
    }
    if (LastMainWindowIndex == 3 && MainWindowIndex != 3)
    {
        // we leave media analyse windows
        Logger::Log(Logger::DEBUG) << "[Changed page] leaving media analyse page!!!" << std::endl;
    }
    if (LastMainWindowIndex == 4 && MainWindowIndex != 4)
    {
        // we leave media AI windows
        Logger::Log(Logger::DEBUG) << "[Changed page] leaving media AI page!!!" << std::endl;
    }
    
    LastMainWindowIndex = MainWindowIndex;
    LastVideoEditorWindowIndex = VideoEditorWindowIndex;
    LastAudioEditorWindowIndex = AudioEditorWindowIndex;
    return updated;
}

static int EditingClip(int type, void* handle)
{
    if (type == MEDIA_VIDEO)
    {
        MainWindowIndex = 1;
        VideoEditorWindowIndex = 0;
    }
    else if (type == MEDIA_AUDIO)
    {
        MainWindowIndex = 2;
        AudioEditorWindowIndex = 0;
    }
    auto updated = UIPageChanged();
    return updated ? 1 : 0;
}

static int EditingOverlap(int type, void* handle)
{
    if (type == MEDIA_VIDEO)
    {
        MainWindowIndex = 1;
        VideoEditorWindowIndex = 1;
    }
    else if (type == MEDIA_AUDIO)
    {
        MainWindowIndex = 2;
        AudioEditorWindowIndex = 1;
    }
    auto updated = UIPageChanged();
    return updated ? 1 : 0;
}

// Utils functions
static bool ExpendButton(ImDrawList *draw_list, ImVec2 pos, bool expand = true)
{
    ImGuiIO &io = ImGui::GetIO();
    ImRect delRect(pos, ImVec2(pos.x + 16, pos.y + 16));
    bool overDel = delRect.Contains(io.MousePos);
    int delColor = IM_COL32_WHITE;
    float midy = pos.y + 16 / 2 - 0.5f;
    float midx = pos.x + 16 / 2 - 0.5f;
    draw_list->AddRect(delRect.Min, delRect.Max, delColor, 4);
    draw_list->AddLine(ImVec2(delRect.Min.x + 3, midy), ImVec2(delRect.Max.x - 4, midy), delColor, 2);
    if (expand) draw_list->AddLine(ImVec2(midx, delRect.Min.y + 3), ImVec2(midx, delRect.Max.y - 4), delColor, 2);
    return overDel;
}

void ShowVideoWindow(ImTextureID texture, ImVec2& pos, ImVec2& size)
{
    if (texture)
    {
        bool bViewisLandscape = size.x >= size.y ? true : false;
        float aspectRatio = (float)ImGui::ImGetTextureWidth(texture) / (float)ImGui::ImGetTextureHeight(texture);
        bool bRenderisLandscape = aspectRatio > 1.f ? true : false;
        bool bNeedChangeScreenInfo = bViewisLandscape ^ bRenderisLandscape;
        float adj_w = bNeedChangeScreenInfo ? size.y : size.x;
        float adj_h = bNeedChangeScreenInfo ? size.x : size.y;
        float adj_x = adj_h * aspectRatio;
        float adj_y = adj_h;
        if (adj_x > adj_w) { adj_y *= adj_w / adj_x; adj_x = adj_w; }
        float offset_x = pos.x + (size.x - adj_x) / 2.0;
        float offset_y = pos.y + (size.y - adj_y) / 2.0;
        ImGui::GetWindowDrawList()->AddImage(
            texture,
            ImVec2(offset_x, offset_y),
            ImVec2(offset_x + adj_x, offset_y + adj_y),
            ImVec2(0, 0),
            ImVec2(1, 1)
        );
    }
}

static void ShowVideoWindow(ImTextureID texture, ImVec2& pos, ImVec2& size, float& offset_x, float& offset_y, float& tf_x, float& tf_y)
{
    if (texture)
    {
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton(("##video_window" + std::to_string((long long)texture)).c_str(), size);
        bool bViewisLandscape = size.x >= size.y ? true : false;
        float aspectRatio = (float)ImGui::ImGetTextureWidth(texture) / (float)ImGui::ImGetTextureHeight(texture);
        bool bRenderisLandscape = aspectRatio > 1.f ? true : false;
        bool bNeedChangeScreenInfo = bViewisLandscape ^ bRenderisLandscape;
        float adj_w = bNeedChangeScreenInfo ? size.y : size.x;
        float adj_h = bNeedChangeScreenInfo ? size.x : size.y;
        float adj_x = adj_h * aspectRatio;
        float adj_y = adj_h;
        if (adj_x > adj_w) { adj_y *= adj_w / adj_x; adj_x = adj_w; }
        tf_x = (size.x - adj_x) / 2.0;
        tf_y = (size.y - adj_y) / 2.0;
        offset_x = pos.x + tf_x;
        offset_y = pos.y + tf_y;
        ImGui::GetWindowDrawList()->AddImage(
            texture,
            ImVec2(offset_x, offset_y),
            ImVec2(offset_x + adj_x, offset_y + adj_y),
            ImVec2(0, 0),
            ImVec2(1, 1)
        );
    }
}

static void MonitorButton(const char * label, ImVec2 pos, int& monitor_index, int disabled_index = -1)
{
    static std::string monitor_icons[] = {ICON_ONE, ICON_TWO, ICON_THREE, ICON_FOUR, ICON_FIVE, ICON_SIX, ICON_SEVEN, ICON_EIGHT, ICON_NINE};
    auto platform_io = ImGui::GetPlatformIO();
    ImGuiViewportP* viewport = (ImGuiViewportP*)ImGui::GetWindowViewport();
    auto current_monitor = viewport->PlatformMonitor;
    ImGui::SetCursorScreenPos(pos);
    for (int monitor_n = 0; monitor_n < platform_io.Monitors.Size; monitor_n++)
    {
        bool disable = false;
        if (disabled_index != -1 && monitor_n == disabled_index)
            disable = true;
        ImGui::BeginDisabled(disable);
        bool selected = monitor_index == monitor_n || (monitor_index == -1 && monitor_n == current_monitor);
        std::string monitor_label = monitor_icons[monitor_n] + "##monitor_index" + std::string(label);
        if (ImGui::CheckButton(monitor_label.c_str(), &selected))
        {
            if (monitor_n == current_monitor)
            { 
                monitor_index = -1;
            }
            else
            {
                monitor_index = monitor_n;
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
        {
            ImGuiPlatformMonitor& mon = platform_io.Monitors[monitor_n];
            ImGui::BeginTooltip();
            ImGui::BulletText("Monitor #%d:", monitor_n);
            ImGui::Text("DPI %.0f", mon.DpiScale * 100.0f);
            ImGui::Text("MainSize (%.0f,%.0f)", mon.MainSize.x, mon.MainSize.y);
            ImGui::Text("WorkSize (%.0f,%.0f)", mon.WorkSize.x, mon.WorkSize.y);
            ImGui::Text("MainMin (%.0f,%.0f)",  mon.MainPos.x,  mon.MainPos.y);
            ImGui::Text("MainMax (%.0f,%.0f)",  mon.MainPos.x + mon.MainSize.x, mon.MainPos.y + mon.MainSize.y);
            ImGui::Text("WorkMin (%.0f,%.0f)",  mon.WorkPos.x,  mon.WorkPos.y);
            ImGui::Text("WorkMax (%.0f,%.0f)",  mon.WorkPos.x + mon.WorkSize.x, mon.WorkPos.y + mon.WorkSize.y);
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
    }
}

// System view
static void ShowAbout()
{
    ImGui::Text("Media Editor Demo(ImGui)");
    ImGui::Separator();
    ImGui::Text("  TanluTeam 2022");
    ImGui::Separator();
    ImGui::ShowImGuiInfo();
    ImGui::Separator();
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", ImGui::GetIO().DeltaTime * 1000.f, ImGui::GetIO().Framerate);
    ImGui::Text("Frames since last input: %d", ImGui::GetIO().FrameCountSinceLastInput);
}

static int GetResolutionIndex(MediaEditorSettings & config)
{
    if (config.VideoWidth == 720 && config.VideoHeight == 480)
        return 1;
    else if (config.VideoWidth == 720 && config.VideoHeight == 576)
        return 2;
    else if (config.VideoWidth == 1280 && config.VideoHeight == 720)
        return 3;
    else if (config.VideoWidth == 1920 && config.VideoHeight == 1080)
        return 4;
    else if (config.VideoWidth == 3840 && config.VideoHeight == 2160)
        return 5;
    else if (config.VideoWidth == 7680 && config.VideoHeight == 3420)
        return 6;
    return 0;
}

static void SetResolution(MediaEditorSettings & config, int index)
{
    switch (index)
    {
        case 1: config.VideoWidth = 720; config.VideoHeight = 480; break;
        case 2: config.VideoWidth = 720; config.VideoHeight = 576; break;
        case 3: config.VideoWidth = 1280; config.VideoHeight = 720; break;
        case 4: config.VideoWidth = 1920; config.VideoHeight = 1080; break;
        case 5: config.VideoWidth = 3840; config.VideoHeight = 2160; break;
        case 6: config.VideoWidth = 7680; config.VideoHeight = 3420; break;
        default: break;
    }
}

static int GetPixelAspectRatioIndex(MediaEditorSettings & config)
{
    if (config.PixelAspectRatio.num == 1 && config.PixelAspectRatio.den == 1)
        return 1;
    else if (config.PixelAspectRatio.num == 16 && config.PixelAspectRatio.den == 9)
        return 2;
    else if (config.PixelAspectRatio.num == 4 && config.PixelAspectRatio.den == 3)
        return 3;
    else if (config.PixelAspectRatio.num == 235 && config.PixelAspectRatio.den == 100)
        return 4;
    else if (config.PixelAspectRatio.num == 137 && config.PixelAspectRatio.den == 100)
        return 5;
    else if (config.PixelAspectRatio.num == 185 && config.PixelAspectRatio.den == 100)
        return 6;
    return 0;
}

static void SetPixelAspectRatio(MediaEditorSettings & config, int index)
{
    switch (index)
    {
        case 1: config.PixelAspectRatio.num = 1;   config.PixelAspectRatio.den = 1; break;
        case 2: config.PixelAspectRatio.num = 16;  config.PixelAspectRatio.den = 9; break;
        case 3: config.PixelAspectRatio.num = 4;   config.PixelAspectRatio.den = 3; break;
        case 4: config.PixelAspectRatio.num = 235; config.PixelAspectRatio.den = 100; break;
        case 5: config.PixelAspectRatio.num = 137; config.PixelAspectRatio.den = 100; break;
        case 6: config.PixelAspectRatio.num = 185; config.PixelAspectRatio.den = 100; break;
        default: break;
    }
}

static int GetVideoFrameIndex(MediaEditorSettings & config)
{
    if (config.VideoFrameRate.num == 24000 && config.VideoFrameRate.den == 1001)
        return 1;
    else if (config.VideoFrameRate.num == 24000 && config.VideoFrameRate.den == 1000)
        return 2;
    else if (config.VideoFrameRate.num == 25000 && config.VideoFrameRate.den == 1000)
        return 3;
    else if (config.VideoFrameRate.num == 30000 && config.VideoFrameRate.den == 1001)
        return 4;
    else if (config.PixelAspectRatio.num == 30000 && config.PixelAspectRatio.den == 1000)
        return 5;
    else if (config.PixelAspectRatio.num == 50000 && config.PixelAspectRatio.den == 1000)
        return 6;
    else if (config.PixelAspectRatio.num == 60000 && config.PixelAspectRatio.den == 1001)
        return 7;
    else if (config.PixelAspectRatio.num == 60000 && config.PixelAspectRatio.den == 1000)
        return 8;
    else if (config.PixelAspectRatio.num == 100000 && config.PixelAspectRatio.den == 1000)
        return 9;
    else if (config.PixelAspectRatio.num == 120000 && config.PixelAspectRatio.den == 1000)
        return 10;
    return 0;
}

static void SetVideoFrameRate(MediaEditorSettings & config, int index)
{
    switch (index)
    {
        case  1: config.VideoFrameRate.num = 24000;  config.VideoFrameRate.den = 1001; break;
        case  2: config.VideoFrameRate.num = 24000;  config.VideoFrameRate.den = 1000; break;
        case  3: config.VideoFrameRate.num = 25000;  config.VideoFrameRate.den = 1000; break;
        case  4: config.VideoFrameRate.num = 30000;  config.VideoFrameRate.den = 1001; break;
        case  5: config.VideoFrameRate.num = 30000;  config.VideoFrameRate.den = 1000; break;
        case  6: config.VideoFrameRate.num = 50000;  config.VideoFrameRate.den = 1000; break;
        case  7: config.VideoFrameRate.num = 60000;  config.VideoFrameRate.den = 1001; break;
        case  8: config.VideoFrameRate.num = 60000;  config.VideoFrameRate.den = 1000; break;
        case  9: config.VideoFrameRate.num = 100000; config.VideoFrameRate.den = 1000; break;
        case 10: config.VideoFrameRate.num = 120000; config.VideoFrameRate.den = 1000; break;
        default: break;
    }
}

static int GetSampleRateIndex(MediaEditorSettings & config)
{
    switch (config.AudioSampleRate)
    {
        case 8000:  return 0;
        case 16000: return 1;
        case 32000: return 2;
        case 44100: return 3;
        case 48000: return 4;
        case 96000: return 5;
        default: return 3;
    }
}

static void SetSampleRate(MediaEditorSettings & config, int index)
{
    switch (index)
    {
        case 0: config.AudioSampleRate =  8000; break;
        case 1: config.AudioSampleRate = 16000; break;
        case 2: config.AudioSampleRate = 32000; break;
        case 3: config.AudioSampleRate = 44100; break;
        case 4: config.AudioSampleRate = 48000; break;
        case 5: config.AudioSampleRate = 96000; break;
        default:config.AudioSampleRate = 44100; break;
    }
}

static int GetChannelIndex(MediaEditorSettings & config)
{
    switch (config.AudioChannels)
    {
        case  1: return 0;
        case  2: return 1;
        case  6: return 2;
        case  8: return 3;
        case 11: return 4;
        case 13: return 5;
        default: return 1;
    }
}

static void SetAudioChannel(MediaEditorSettings & config, int index)
{
    switch (index)
    {
        case 0: config.AudioChannels =  1; break;
        case 1: config.AudioChannels =  2; break;
        case 2: config.AudioChannels =  6; break;
        case 3: config.AudioChannels =  8; break;
        case 4: config.AudioChannels = 11; break;
        case 5: config.AudioChannels = 13; break;
        default:config.AudioChannels =  2; break;
    }
}

static int GetAudioFormatIndex(MediaEditorSettings & config)
{
    switch (config.AudioFormat)
    {
        case  1: return 0;
        case  2: return 1;
        case  3: return 2;
        default: return 1;
    }
}

static void SetAudioFormat(MediaEditorSettings & config, int index)
{
    switch (index)
    {
        case 0: config.AudioFormat =  1; break;
        case 1: config.AudioFormat =  2; break;
        case 2: config.AudioFormat =  3; break;
        default:config.AudioFormat =  2; break;
    }
}

static void ShowConfigure(MediaEditorSettings & config)
{
    const char* resolution_items[] = { "Custom", "720x480 NTSC", "720x576 PAL", "1280x720 HD", "1920x1080 HD", "3840x2160 UHD", "7680x3420 8K UHD"};
    const char* pixel_aspect_items[] = { "Custom", "Square", "16:9", "4:3", "Cinemascope", "Academy Standard", "Academy Flat" }; // Cinemascope=2.35:1 Academy Standard=1.37:1 Academy Flat=1.85:1
    const char* frame_rate_items[] = { "Custom", "23.976", "24", "25", "29.97", "30", "50", "59.94", "60", "100", "120" };
    const char* audio_sample_rate_items[] = { "8k", "16k", "32k", "44.1k", "48k", "96k" };
    const char* audio_channels_items[] = { "Mono", "Stereo", "Surround Stereo 5.1", "Surround Stereo 7.1", "Surround Stereo 10.1", "Surround Stereo 12.1"};
    const char* audio_format_items[] = { "16bit Short", "32bit Float", "64bit Double" };
    
    static int resolution_index = GetResolutionIndex(config);
    static int pixel_aspect_index = GetPixelAspectRatioIndex(config);
    static int frame_rate_index = GetVideoFrameIndex(config);
    static int sample_rate_index = GetSampleRateIndex(config);
    static int channels_index = GetChannelIndex(config);
    static int format_index = GetAudioFormatIndex(config);

    static char buf_cache_size[64] = {0}; sprintf(buf_cache_size, "%d", config.VideoFrameCacheSize);
    static char buf_res_x[64] = {0}; sprintf(buf_res_x, "%d", config.VideoWidth);
    static char buf_res_y[64] = {0}; sprintf(buf_res_y, "%d", config.VideoHeight);
    static char buf_par_x[64] = {0}; sprintf(buf_par_x, "%d", config.PixelAspectRatio.num);
    static char buf_par_y[64] = {0}; sprintf(buf_par_y, "%d", config.PixelAspectRatio.den);
    static char buf_fmr_x[64] = {0}; sprintf(buf_fmr_x, "%d", config.VideoFrameRate.num);
    static char buf_fmr_y[64] = {0}; sprintf(buf_fmr_y, "%d", config.VideoFrameRate.den);

    static const int numConfigureTabs = sizeof(ConfigureTabNames)/sizeof(ConfigureTabNames[0]);
    if (ImGui::BeginChild("##ConfigureView", ImVec2(800, 600), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TabLabels(numConfigureTabs, ConfigureTabNames, ConfigureIndex, nullptr , false, false, nullptr, nullptr, false, false, nullptr, nullptr);
        switch (ConfigureIndex)
        {
            case 0:
                // system setting
                ImGui::TextUnformatted("Show UI Help Tips");
                ImGui::ToggleButton("##show_ui_help_tooltips", &config.ShowHelpTooltips);
                ImGui::Separator();
                ImGui::TextUnformatted("Bank View Style");
                ImGui::RadioButton("Icons",  (int *)&config.BankViewStyle, 0); ImGui::SameLine();
                ImGui::RadioButton("Tree",  (int *)&config.BankViewStyle, 1);
                ImGui::TextUnformatted("Video Frame Cache Size");
                ImGui::PushItemWidth(60);
                ImGui::InputText("##Video_cache_size", buf_cache_size, 64, ImGuiInputTextFlags_CharsDecimal);
                config.VideoFrameCacheSize = atoi(buf_cache_size);
            break;
            case 1:
            {
                // timeline setting
                if (ImGui::Combo("Resultion", &resolution_index, resolution_items, IM_ARRAYSIZE(resolution_items)))
                {
                    SetResolution(config, resolution_index);
                }
                ImGui::BeginDisabled(resolution_index != 0);
                ImGui::PushItemWidth(60);
                ImGui::InputText("##Resultion_x", buf_res_x, 64, ImGuiInputTextFlags_CharsDecimal);
                ImGui::SameLine();
                ImGui::TextUnformatted("X");
                ImGui::SameLine();
                ImGui::InputText("##Resultion_y", buf_res_y, 64, ImGuiInputTextFlags_CharsDecimal);
                ImGui::PopItemWidth();
                ImGui::EndDisabled();
                if (resolution_index == 0)
                {
                    config.VideoWidth = atoi(buf_res_x);
                    config.VideoHeight = atoi(buf_res_y);
                }

                if (ImGui::Combo("Pixel Aspect Ratio", &pixel_aspect_index, pixel_aspect_items, IM_ARRAYSIZE(pixel_aspect_items)))
                {
                    SetPixelAspectRatio(config, pixel_aspect_index);
                }
                ImGui::BeginDisabled(pixel_aspect_index != 0);
                ImGui::PushItemWidth(60);
                ImGui::InputText("##PixelAspectRatio_x", buf_par_x, 64, ImGuiInputTextFlags_CharsDecimal);
                ImGui::SameLine();
                ImGui::TextUnformatted(":");
                ImGui::SameLine();
                ImGui::InputText("##PixelAspectRatio_y", buf_par_y, 64, ImGuiInputTextFlags_CharsDecimal);
                ImGui::PopItemWidth();
                ImGui::EndDisabled();
                if (pixel_aspect_index == 0)
                {
                    config.PixelAspectRatio.num = atoi(buf_par_x);
                    config.PixelAspectRatio.den = atoi(buf_par_y); // TODO::Dicky need check den != 0
                }

                if (ImGui::Combo("Video Frame Rate", &frame_rate_index, frame_rate_items, IM_ARRAYSIZE(frame_rate_items)))
                {
                    SetVideoFrameRate(config, frame_rate_index);
                }
                ImGui::BeginDisabled(frame_rate_index != 0);
                ImGui::PushItemWidth(60);
                ImGui::InputText("##VideoFrameRate_x", buf_fmr_x, 64, ImGuiInputTextFlags_CharsDecimal);
                ImGui::SameLine();
                ImGui::TextUnformatted(":");
                ImGui::SameLine();
                ImGui::InputText("##VideoFrameRate_y", buf_fmr_y, 64, ImGuiInputTextFlags_CharsDecimal);
                ImGui::PopItemWidth();
                ImGui::EndDisabled();
                if (frame_rate_index == 0)
                {
                    config.VideoFrameRate.num = atoi(buf_fmr_x);
                    config.VideoFrameRate.den = atoi(buf_fmr_y); // TODO::Dicky need check den != 0
                }

                ImGui::Separator();

                if (ImGui::Combo("Audio Sample Rate", &sample_rate_index, audio_sample_rate_items, IM_ARRAYSIZE(audio_sample_rate_items)))
                {
                    SetSampleRate(config, sample_rate_index);
                }
                if (ImGui::Combo("Audio Channels", &channels_index, audio_channels_items, IM_ARRAYSIZE(audio_channels_items)))
                {
                    SetAudioChannel(config, channels_index);
                }
                if (ImGui::Combo("Audio Format", &format_index, audio_format_items, IM_ARRAYSIZE(audio_format_items)))
                {
                    SetAudioFormat(config, format_index);
                }
            }
            break;
            default: break;
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
}

// Document Framework
static void NewTimeline()
{
    timeline = new TimeLine();
    if (timeline)
    {
        timeline->mWidth = g_media_editor_settings.VideoWidth;
        timeline->mHeight = g_media_editor_settings.VideoHeight;
        timeline->mFrameRate = g_media_editor_settings.VideoFrameRate;
        timeline->mMaxCachedVideoFrame = g_media_editor_settings.VideoFrameCacheSize > 0 ? g_media_editor_settings.VideoFrameCacheSize : 10;
        timeline->mAudioSampleRate = g_media_editor_settings.AudioSampleRate;
        timeline->mAudioChannels = g_media_editor_settings.AudioChannels;
        timeline->mAudioFormat = (AudioRender::PcmFormat)g_media_editor_settings.AudioFormat;
        timeline->mShowHelpTooltips = g_media_editor_settings.ShowHelpTooltips;
        
        // init callbacks
        timeline->m_CallBacks.EditingClip = EditingClip;
        timeline->m_CallBacks.EditingOverlap = EditingOverlap;

        // init bp view
        float labelWidth = ImGui::CalcVerticalTabLabelsWidth() + 4;
        ImVec2 view_size = ImVec2(DEFAULT_MAIN_VIEW_WIDTH * 0.8 * 1 / 3 - labelWidth, DEFAULT_MAIN_VIEW_HEIGHT * 0.6);
        if (timeline->mVideoFilterBluePrint)
        {
            timeline->mVideoFilterBluePrint->m_ViewSize = view_size;
        }
        if (timeline->mVideoFusionBluePrint)
        {
            timeline->mVideoFusionBluePrint->m_ViewSize = view_size;
        }
        if (timeline->mAudioFilterBluePrint)
        {
            timeline->mAudioFilterBluePrint->m_ViewSize = view_size;
        }
        if (timeline->mAudioFusionBluePrint)
        {
            timeline->mAudioFusionBluePrint->m_ViewSize = view_size;
        }
    }
}

static void CleanProject()
{
    if (timeline)
    {
        delete timeline;
        timeline = nullptr;
    }
    NewTimeline();
    g_project = imgui_json::value();
}

static void NewProject()
{
    Logger::Log(Logger::DEBUG) << "[Project] Create new project!!!" << std::endl;
    CleanProject();
    g_media_editor_settings.project_path = "";
    quit_save_confirm = true;
}

static int LoadProject(std::string path)
{
    Logger::Log(Logger::DEBUG) << "[Project] Load project from file!!!" << std::endl;
    CleanProject();

    auto loadResult = imgui_json::value::load(path);
    if (!loadResult.second)
        return -1;

    // first load media bank
    auto project = loadResult.first;
    const imgui_json::array* mediaBankArray = nullptr;
    if (BluePrint::GetPtrTo(project, "MediaBank", mediaBankArray))
    {
        for (auto& media : *mediaBankArray)
        {
            MediaItem * item = nullptr;
            int64_t id = -1;
            std::string name;
            std::string path;
            MediaTimeline::MEDIA_TYPE type = MEDIA_UNKNOWN;
            if (media.contains("id"))
            {
                auto& val = media["id"];
                if (val.is_number())
                {
                    id = val.get<imgui_json::number>();
                }
            }
            if (media.contains("name"))
            {
                auto& val = media["name"];
                if (val.is_string())
                {
                    name = val.get<imgui_json::string>();
                }
            }
            if (media.contains("path"))
            {
                auto& val = media["path"];
                if (val.is_string())
                {
                    path = val.get<imgui_json::string>();
                }
            }
            if (media.contains("type"))
            {
                auto& val = media["type"];
                if (val.is_number())
                {
                    type = (MediaTimeline::MEDIA_TYPE)val.get<imgui_json::number>();
                }
            }
            
            item = new MediaItem(name, path, type, timeline);
            if (id != -1) item->mID = id;
            timeline->media_items.push_back(item);
        }
    }

    // second load TimeLine
    if (timeline && project.contains("TimeLine"))
    {
        auto& val = project["TimeLine"];
        timeline->Load(val);
    }

    g_media_editor_settings.project_path = path;
    quit_save_confirm = false;

    return 0;
}

static void SaveProject(std::string path)
{
    if (!timeline || path.empty())
        return;

    Logger::Log(Logger::DEBUG) << "[Project] Save project to file!!!" << std::endl;
    // TODO::Dicky stop all play
    timeline->Play(false, true);

    // check current editing clip, if it has bp then save it to clip
    Clip * editing_clip = timeline->FindEditingClip();
    if (editing_clip)
    {
        switch (editing_clip->mType)
        {
            case MEDIA_VIDEO:
                if (timeline->mVideoFilterBluePrint && timeline->mVideoFilterBluePrint->m_Document->m_Blueprint.IsOpened()) 
                    editing_clip->mFilterBP = timeline->mVideoFilterBluePrint->m_Document->Serialize();
            break;
            case MEDIA_AUDIO:
                if (timeline->mAudioFilterBluePrint && timeline->mAudioFilterBluePrint->m_Document->m_Blueprint.IsOpened()) 
                    editing_clip->mFilterBP = timeline->mAudioFilterBluePrint->m_Document->Serialize();
            break;
            default: break;
        }
    }

    // check current editing overlap, if it has bp then save it to overlap
    Overlap * editing_overlap = timeline->FindEditingOverlap();
    if (editing_overlap)
    {
        auto clip = timeline->FindClipByID(editing_overlap->m_Clip.first);
        if (clip)
        {
            switch (clip->mType)
            {
                case MEDIA_VIDEO:
                    if (timeline->mVideoFusionBluePrint && timeline->mVideoFusionBluePrint->m_Document->m_Blueprint.IsOpened()) 
                        editing_overlap->mFusionBP = timeline->mVideoFusionBluePrint->m_Document->Serialize();
                break;
                default:
                break;
            }
        }
    }

    // first save media bank info
    imgui_json::value media_bank;
    for (auto media : timeline->media_items)
    {
        imgui_json::value item;
        item["id"] = imgui_json::number(media->mID);
        item["name"] = media->mName;
        item["path"] = media->mPath;
        item["type"] = imgui_json::number(media->mMediaType);
        media_bank.push_back(item);
    }
    g_project["MediaBank"] = media_bank;

    // second save Timeline
    imgui_json::value timeline_val;
    timeline->Save(timeline_val);
    g_project["TimeLine"] = timeline_val;

    g_project.save(path);
    g_media_editor_settings.project_path = path;
    quit_save_confirm = false;
}

/****************************************************************************************
 * 
 * Media Bank window
 *
 ***************************************************************************************/
static inline std::string GetVideoIcon(int width, int height)
{
    if (width == 320 && height == 240) return "QVGA";
    else if (width == 176 && height == 144) return "QCIF";
    else if (width == 352 && height == 288) return "CIF";
    else if ((width == 720 && height == 576) || (width == 704 && height == 576)) return "D1";
    else if (width == 640 && height == 480) return "VGA";
    else if (width == 1280 && height == 720) return ICON_1K;
    else if (height >= 1080 && height <= 1088) return ICON_2K;
    else if (height == 1836) return ICON_3K;
    else if (height == 2160) return ICON_4K_PLUS;
    else if (height == 2700) return ICON_5K;
    else if (height == 3240) return ICON_6K;
    else if (height == 3780) return ICON_7K;
    else if (height == 4320) return ICON_8K;
    else if (height == 4860) return ICON_9K;
    else if (height == 5400) return ICON_10K;
    else 
    {
        if (height > 720  && height < 1080) return ICON_1K_PLUS;
        if (height > 1088  && height < 1836) return ICON_2K_PLUS;
        if (height > 1836  && height < 2160) return ICON_3K_PLUS;
        if (height > 2160  && height < 2700) return ICON_4K_PLUS;
        if (height > 2700  && height < 3240) return ICON_5K_PLUS;
        if (height > 3240  && height < 3780) return ICON_6K_PLUS;
        if (height > 3780  && height < 4320) return ICON_7K_PLUS;
        if (height > 4320  && height < 4860) return ICON_8K_PLUS;
        if (height > 4860  && height < 5400) return ICON_9K_PLUS;
    }
    return ICON_MEDIA_VIDEO;
}

static inline std::string GetAudioChannelName(int channels)
{
    if (channels < 2) return "Mono";
    else if (channels == 2) return "Stereo";
    else if (channels == 6) return "Surround 5.1";
    else if (channels == 8) return "Surround 7.1";
    else if (channels == 10) return "Surround 9.1";
    else if (channels == 13) return "Surround 12.1";
    else return "Channels " + std::to_string(channels);
}

static std::vector<MediaItem *>::iterator InsertMediaIcon(std::vector<MediaItem *>::iterator item, ImDrawList *draw_list, ImVec2 icon_pos, float media_icon_size)
{
    ImTextureID texture = nullptr;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    (*item)->UpdateThumbnail();
    ImVec2 icon_size = ImVec2(media_icon_size, media_icon_size);
    // Draw Shadow for Icon
    draw_list->AddRectFilled(icon_pos + ImVec2(6, 6), icon_pos + ImVec2(6, 6) + icon_size, IM_COL32(32, 32, 32, 255));
    draw_list->AddRectFilled(icon_pos + ImVec2(4, 4), icon_pos + ImVec2(4, 4) + icon_size, IM_COL32(48, 48, 72, 255));
    draw_list->AddRectFilled(icon_pos + ImVec2(2, 2), icon_pos + ImVec2(2, 2) + icon_size, IM_COL32(64, 64, 96, 255));
    ImGui::SetCursorScreenPos(icon_pos);
    ImGui::InvisibleButton((*item)->mPath.c_str(), icon_size);
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        ImGui::SetDragDropPayload("Media_drag_drop", *item, sizeof(MediaItem));
        ImGui::TextUnformatted((*item)->mName.c_str());
        if (!(*item)->mMediaThumbnail.empty() && (*item)->mMediaThumbnail[0])
        {
            auto tex_w = ImGui::ImGetTextureWidth((*item)->mMediaThumbnail[0]);
            auto tex_h = ImGui::ImGetTextureHeight((*item)->mMediaThumbnail[0]);
            float aspectRatio = (float)tex_w / (float)tex_h;
            ImGui::Image((*item)->mMediaThumbnail[0], ImVec2(icon_size.x, icon_size.y / aspectRatio));
        }
        ImGui::EndDragDropSource();
    }
    if (ImGui::IsItemHovered())
    {
        float pos_x = io.MousePos.x - icon_pos.x;
        float percent = pos_x / icon_size.x;
        ImClamp(percent, 0.0f, 1.0f);
        int texture_index = (*item)->mMediaThumbnail.size() * percent;
        if ((*item)->mMediaType == MEDIA_PICTURE)
            texture_index = 0;
        if (!(*item)->mMediaThumbnail.empty())
        {
            texture = (*item)->mMediaThumbnail[texture_index];
        }

        // Show help tooltip
        if (timeline->mShowHelpTooltips)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Help:");
            ImGui::TextUnformatted("    Slider mouse to overview");
            ImGui::TextUnformatted("    Drag media to timeline");
            ImGui::EndTooltip();
            ImGui::PopStyleVar();
        }
    }
    else if (!(*item)->mMediaThumbnail.empty())
    {
        if ((*item)->mMediaThumbnail.size() > 1)
            texture = (*item)->mMediaThumbnail[1];
        else
            texture = (*item)->mMediaThumbnail[0];
    }

    ImGui::SetCursorScreenPos(icon_pos);
    if (texture)
    {
        auto tex_w = ImGui::ImGetTextureWidth(texture);
        auto tex_h = ImGui::ImGetTextureHeight(texture);
        float aspectRatio = (float)tex_w / (float)tex_h;
        bool bViewisLandscape = icon_size.x >= icon_size.y ? true : false;
        bool bRenderisLandscape = aspectRatio > 1.f ? true : false;
        bool bNeedChangeScreenInfo = bViewisLandscape ^ bRenderisLandscape;
        float adj_w = bNeedChangeScreenInfo ? icon_size.y : icon_size.x;
        float adj_h = bNeedChangeScreenInfo ? icon_size.x : icon_size.y;
        float adj_x = adj_h * aspectRatio;
        float adj_y = adj_h;
        if (adj_x > adj_w) { adj_y *= adj_w / adj_x; adj_x = adj_w; }
        float offset_x = (icon_size.x - adj_x) / 2.0;
        float offset_y = (icon_size.y - adj_y) / 2.0;
        ImGui::PushID((void*)(intptr_t)texture);
        const ImGuiID id = ImGui::GetCurrentWindow()->GetID("#image");
        ImGui::PopID();
        ImGui::ImageButtonEx(id, texture, ImVec2(adj_w - offset_x * 2, adj_h - offset_y * 2), 
                            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec2(offset_x, offset_y),
                            ImVec4(0.0f, 0.0f, 0.0f, 1.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    else
    {
        if ((*item)->mMediaType == MEDIA_AUDIO && (*item)->mMediaOverview)
        {
            auto wavefrom = (*item)->mMediaOverview->GetWaveform();
            if (wavefrom && wavefrom->pcm.size() > 0)
            {
                ImVec2 wave_pos = icon_pos + ImVec2(4, 28);
                ImVec2 wave_size = icon_size - ImVec2(8, 48);
                ImGui::SetCursorScreenPos(icon_pos);
                float wave_range = fmax(fabs(wavefrom->minSample), fabs(wavefrom->maxSample));
                int channels = wavefrom->pcm.size();
                if (channels > 2) channels = 2;
                int channel_height = wave_size.y / channels;
                ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.f, 1.f, 0.f, 1.0f));
                for (int i = 0; i < channels; i++)
                {
                    ImGui::SetCursorScreenPos(wave_pos + ImVec2(0, i * channel_height));
                    ImVec2 plot_size(wave_size.x, channel_height);
                    int sampleSize = wavefrom->pcm[i].size();
                    std::string id_string = "##BankWaveform@" + std::to_string((*item)->mID) + "@" + std::to_string(i);
                    ImGui::PlotLines(id_string.c_str(), &wavefrom->pcm[i][0], sampleSize, 0, nullptr, -wave_range / 2, wave_range / 2, plot_size, sizeof(float), false);
                }
                ImGui::PopStyleColor(2);
            }
            else
            {
                std::string lable = std::string(ICON_MEDIA_WAVE) + "##" + (*item)->mName + "@" + std::to_string((*item)->mID);
                ImGui::SetWindowFontScale(2.5);
                ImGui::Button(lable.c_str(), ImVec2(media_icon_size, media_icon_size));
                ImGui::SetWindowFontScale(1.0);
            }
        }
        else
            ImGui::Button((*item)->mName.c_str(), ImVec2(media_icon_size, media_icon_size));
    }

    if ((*item)->mMediaOverview && (*item)->mMediaOverview->IsOpened())
    {
        auto has_video = (*item)->mMediaOverview->HasVideo();
        auto has_audio = (*item)->mMediaOverview->HasAudio();
        auto media_length = (*item)->mMediaOverview->GetMediaParser()->GetMediaInfo()->duration;
        ImGui::SetCursorScreenPos(icon_pos + ImVec2(4, 4));
        std::string type_string = "? ";
        switch ((*item)->mMediaType)
        {
            case MEDIA_UNKNOWN: break;
            case MEDIA_VIDEO: type_string = std::string(ICON_FA5_FILE_VIDEO) + " "; break;
            case MEDIA_AUDIO: type_string = std::string(ICON_FA5_FILE_AUDIO) + " "; break;
            case MEDIA_PICTURE: type_string = std::string(ICON_FA5_FILE_IMAGE) + " "; break;
            case MEDIA_TEXT: type_string = std::string(ICON_FA5_FILE_CODE) + " "; break;
            default: break;
        }
        type_string += TimelineMillisecToString(media_length * 1000, 2);
        ImGui::SetWindowFontScale(0.7);
        ImGui::TextUnformatted(type_string.c_str());
        ImGui::SetWindowFontScale(1.0);
        ImGui::ShowTooltipOnHover("%s", (*item)->mPath.c_str());
        ImGui::SetCursorScreenPos(icon_pos + ImVec2(0, media_icon_size - 20));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
        if (has_video)
        {
            auto stream = (*item)->mMediaOverview->GetVideoStream();
            if (stream)
            {
                auto video_icon = GetVideoIcon(stream->width, stream->height);
                ImGui::SetWindowFontScale(1.2);
                ImGui::Button(video_icon.c_str(), ImVec2(24, 24));
                if (ImGui::IsItemHovered())
                {
                    std::string bitrate_str = stream->bitRate >= 1000000 ? std::to_string((float)stream->bitRate / 1000000) + "Mbps" :
                                            stream->bitRate >= 1000 ? std::to_string((float)stream->bitRate / 1000) + "Kbps" :
                                            std::to_string(stream->bitRate) + "bps";
                    ImGui::BeginTooltip();
                    ImGui::Text("S: %d x %d", stream->width, stream->height);
                    ImGui::Text("B: %s", bitrate_str.c_str());
                    ImGui::Text("F: %.3ffps", stream->avgFrameRate.den > 0 ? stream->avgFrameRate.num / stream->avgFrameRate.den : 0.0);
                    ImGui::EndTooltip();
                }
                ImGui::SameLine(0, 0);
                if (stream->isHdr)
                {
                    ImGui::Button(ICON_HDR, ImVec2(24, 24));
                    ImGui::SameLine(0, 0);
                }
                ImGui::SetWindowFontScale(0.6);
                ImGui::Button((std::to_string(stream->bitDepth) + "bit").c_str(), ImVec2(24, 24));
                ImGui::SetWindowFontScale(1.0);
                ImGui::SameLine(0, 0);
            }
        }
        if (has_audio)
        {
            auto stream = (*item)->mMediaOverview->GetAudioStream();
            if (stream)
            {
                auto audio_channels = stream->channels;
                auto audio_sample_rate = stream->sampleRate;
                std::string audio_icon = audio_channels >= 2 ? ICON_STEREO : ICON_MONO;
                ImGui::Button(audio_icon.c_str(), ImVec2(24, 24));
                ImGui::ShowTooltipOnHover("%d %s", audio_sample_rate, GetAudioChannelName(audio_channels).c_str());
                ImGui::SameLine(0 ,0);
            }
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::SetCursorScreenPos(icon_pos + ImVec2(media_icon_size - 16, 0));
    ImGui::SetWindowFontScale(0.8);
    ImGui::Button((std::string(ICON_TRASH "##delete_media") + (*item)->mPath).c_str(), ImVec2(16, 16));
    ImGui::SetWindowFontScale(1.0);
    ImRect button_rect(icon_pos + ImVec2(media_icon_size - 16, 0), icon_pos + ImVec2(media_icon_size - 16, 0) + ImVec2(16, 16));
    bool overButton = button_rect.Contains(io.MousePos);
    if (overButton && io.MouseClicked[0])
    {
        // TODO::Dicky need delete it from timeline list ?
        MediaItem * it = *item;
        delete it;
        item = timeline->media_items.erase(item);
    }
    else
        item++;
    ImGui::ShowTooltipOnHover("Delete Media");

    ImGui::PopStyleColor();
    return item;
}

static void ShowMediaBankWindow(ImDrawList *draw_list, float media_icon_size)
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 window_pos = ImGui::GetWindowPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImGui::SetWindowFontScale(2.5);
    ImGui::Indent(20);
    ImGui::PushStyleVar(ImGuiStyleVar_TexGlyphOutlineWidth, 0.5f);
    ImGui::PushStyleColor(ImGuiCol_TexGlyphOutline, ImVec4(0.2, 0.2, 0.2, 0.7));
    draw_list->AddText(window_pos + ImVec2(8, 0), IM_COL32(56, 56, 56, 128), "Media");
    draw_list->AddText(window_pos + ImVec2(8, 32), IM_COL32(56, 56, 56, 128), "Bank");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::SetWindowFontScale(1.0);

    if (!timeline)
        return;
    
    if (timeline->media_items.empty())
    {
        ImGui::SetWindowFontScale(2.0);
        ImGui::Indent(20);
        ImGui::PushStyleVar(ImGuiStyleVar_TexGlyphOutlineWidth, 0.5f);
        ImGui::PushStyleColor(ImGuiCol_TexGlyphOutline, ImVec4(0.2, 0.2, 0.2, 0.7));
        ImU32 text_color = IM_COL32(ui_breathing * 255, ui_breathing * 255, ui_breathing * 255, 255);
        draw_list->AddText(window_pos + ImVec2(8,  72), IM_COL32(56, 56, 56, 128), "Please Click");
        draw_list->AddText(window_pos + ImVec2(8, 104), text_color, "<-- Here");
        draw_list->AddText(window_pos + ImVec2(8, 136), IM_COL32(56, 56, 56, 128), "To Add Media");
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::SetWindowFontScale(1.0);
        return;
    }
    // Show Media Icons
    int icon_number_pre_row = window_size.x / (media_icon_size + 24);
    for (auto item = timeline->media_items.begin(); item != timeline->media_items.end();)
    {
        auto icon_pos = ImGui::GetCursorScreenPos() + ImVec2(0, 24);
        for (int i = 0; i < icon_number_pre_row; i++)
        {
            auto row_icon_pos = icon_pos + ImVec2(i * (media_icon_size + 24), 0);
            item = InsertMediaIcon(item, draw_list, row_icon_pos, media_icon_size);
            if (item == timeline->media_items.end())
                break;
        }
        if (item == timeline->media_items.end())
            break;
        ImGui::SetCursorScreenPos(icon_pos + ImVec2(0, media_icon_size));
    }
    ImGui::Dummy(ImVec2(0, 24));
}

/****************************************************************************************
 * 
 * Transition Bank window
 *
 ***************************************************************************************/
static void ShowTransitionBankWindow(ImDrawList *draw_list)
{
    ImGui::SetWindowFontScale(1.2);
    ImGui::Indent(20);
    ImGui::PushStyleVar(ImGuiStyleVar_TexGlyphOutlineWidth, 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4, 0.4, 0.8, 0.8));
    ImGui::TextUnformatted("Transition Bank");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::SetWindowFontScale(1.0);
}

/****************************************************************************************
 * 
 * Filters Bank window
 *
 ***************************************************************************************/
static void ShowFilterBankIconWindow(ImDrawList *draw_list)
{
    float filter_icon_size = 48;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 window_pos = ImGui::GetWindowPos();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImGui::SetWindowFontScale(2.5);
    ImGui::Indent(20);
    ImGui::PushStyleVar(ImGuiStyleVar_TexGlyphOutlineWidth, 0.5f);
    ImGui::PushStyleColor(ImGuiCol_TexGlyphOutline, ImVec4(0.2, 0.2, 0.2, 0.7));
    draw_list->AddText(window_pos + ImVec2(8, 0), IM_COL32(56, 56, 56, 128), "Filter");
    draw_list->AddText(window_pos + ImVec2(8, 48), IM_COL32(56, 56, 56, 128), "Bank");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::SetWindowFontScale(1.0);

    if (!timeline)
        return;
    // Show Filter Icons
    if (timeline->mVideoFilterBluePrint &&
        timeline->mVideoFilterBluePrint->m_Document)
    {
        auto &bp = timeline->mVideoFilterBluePrint->m_Document->m_Blueprint;
        auto node_reg = bp.GetNodeRegistry();
        for (auto type : node_reg->GetTypes())
        {
            auto catalog = BluePrint::GetCatalogInfo(type->m_Catalog);
            if (catalog.size() < 2 || catalog[0].compare("Filter") != 0)
                continue;
            std::string drag_type = "Filter_drag_drop_" + catalog[1];
            ImGui::Dummy(ImVec2(0, 16));
            auto icon_pos = ImGui::GetCursorScreenPos();
            ImVec2 icon_size = ImVec2(filter_icon_size, filter_icon_size);
            // Draw Shadow for Icon
            draw_list->AddRectFilled(icon_pos + ImVec2(6, 6), icon_pos + ImVec2(6, 6) + icon_size, IM_COL32(32, 32, 32, 255));
            draw_list->AddRectFilled(icon_pos + ImVec2(4, 4), icon_pos + ImVec2(4, 4) + icon_size, IM_COL32(48, 48, 72, 255));
            draw_list->AddRectFilled(icon_pos + ImVec2(2, 2), icon_pos + ImVec2(2, 2) + icon_size, IM_COL32(64, 64, 96, 255));
            ImGui::InvisibleButton(type->m_Name.c_str(), icon_size);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                ImGui::SetDragDropPayload(drag_type.c_str(), type, sizeof(BluePrint::NodeTypeInfo));
                ImGui::TextUnformatted(ICON_BANK " Add Filter");
                ImGui::TextUnformatted(type->m_Name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered())
            {
                // Show help tooltip
                if (timeline->mShowHelpTooltips)
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5);
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Help:");
                    ImGui::TextUnformatted("    Drag filter to blue print");
                    ImGui::EndTooltip();
                    ImGui::PopStyleVar();
                }
            }
            ImGui::SetCursorScreenPos(icon_pos);
            ImGui::Button((std::string(ICON_BANK) + "##bank_filter" + type->m_Name).c_str() , ImVec2(filter_icon_size, filter_icon_size));
            ImGui::SameLine(); ImGui::TextUnformatted(type->m_Name.c_str());
        }
    }
}

static void ShowFilterBankTreeWindow(ImDrawList *draw_list)
{
    ImVec2 window_pos = ImGui::GetWindowPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    const ImVec2 item_size(window_size.x, 32);
    ImGui::SetWindowFontScale(2.5);
    ImGui::PushStyleVar(ImGuiStyleVar_TexGlyphOutlineWidth, 0.5f);
    ImGui::PushStyleColor(ImGuiCol_TexGlyphOutline, ImVec4(0.2, 0.2, 0.2, 0.7));
    draw_list->AddText(window_pos + ImVec2(8, 0), IM_COL32(56, 56, 56, 128), "Filter");
    draw_list->AddText(window_pos + ImVec2(8, 48), IM_COL32(56, 56, 56, 128), "Bank");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::SetWindowFontScale(1.0);

    // Show Filter Tree
    if (timeline && timeline->mVideoFilterBluePrint &&
        timeline->mVideoFilterBluePrint->m_Document)
    {
        std::vector<const BluePrint::NodeTypeInfo*> filters;
        auto &bp = timeline->mVideoFilterBluePrint->m_Document->m_Blueprint;
        auto node_reg = bp.GetNodeRegistry();
        // find all filters
        for (auto type : node_reg->GetTypes())
        {
            auto catalog = BluePrint::GetCatalogInfo(type->m_Catalog);
            if (!catalog.size() || catalog[0].compare("Filter") != 0)
                continue;
            filters.push_back(type);
        }

        // make filter type as tree
        ImGui::ImTree filter_tree;
        filter_tree.name = "Filters";
        for (auto type : filters)
        {
            auto catalog = BluePrint::GetCatalogInfo(type->m_Catalog);
            if (!catalog.size())
                continue;
            if (catalog.size() > 1)
            {
                auto children = filter_tree.FindChildren(catalog[1]);
                if (!children)
                {
                    ImGui::ImTree subtree(catalog[1]);
                    if (catalog.size() > 2)
                    {
                        ImGui::ImTree sub_sub_tree(catalog[2]);
                        ImGui::ImTree end_sub(type->m_Name, (void *)type);
                        sub_sub_tree.childrens.push_back(end_sub);
                        subtree.childrens.push_back(sub_sub_tree);
                    }
                    else
                    {
                        ImGui::ImTree end_sub(type->m_Name, (void *)type);
                        subtree.childrens.push_back(end_sub);
                    }

                    filter_tree.childrens.push_back(subtree);
                }
                else
                {
                    if (catalog.size() > 2)
                    {
                        auto sub_children = children->FindChildren(catalog[2]);
                        if (!sub_children)
                        {
                            ImGui::ImTree subtree(catalog[2]);
                            ImGui::ImTree end_sub(type->m_Name, (void *)type);
                            subtree.childrens.push_back(end_sub);
                            children->childrens.push_back(subtree);
                        }
                        else
                        {
                            ImGui::ImTree end_sub(type->m_Name, (void *)type);
                            sub_children->childrens.push_back(end_sub);
                        }
                    }
                    else
                    {
                        ImGui::ImTree end_sub(type->m_Name, (void *)type);
                        children->childrens.push_back(end_sub);
                    }
                }
            }
            else
            {
                ImGui::ImTree end_sub(type->m_Name, (void *)type);
                filter_tree.childrens.push_back(end_sub);
            }
        }

        auto AddFilter = [](void* data)
        {
            const BluePrint::NodeTypeInfo* type = (const BluePrint::NodeTypeInfo*)data;
            auto catalog = BluePrint::GetCatalogInfo(type->m_Catalog);
            if (catalog.size() < 2 || catalog[0].compare("Filter") != 0)
                return;
            std::string drag_type = "Filter_drag_drop_" + catalog[1];
            ImGui::Button((std::string(ICON_BANK) + " " + type->m_Name).c_str(), ImVec2(0, 32));
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                ImGui::SetDragDropPayload(drag_type.c_str(), type, sizeof(BluePrint::NodeTypeInfo));
                ImGui::TextUnformatted(ICON_BANK " Add Filter");
                ImGui::TextUnformatted(type->m_Name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered())
            {
                // Show help tooltip
                if (timeline->mShowHelpTooltips)
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5);
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Help:");
                    ImGui::TextUnformatted("    Drag filter to blue print");
                    ImGui::EndTooltip();
                    ImGui::PopStyleVar();
                }
            }
        };

        // draw filter tree
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        for (auto sub : filter_tree.childrens)
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (sub.data)
            {
                AddFilter(sub.data);
            }
            else if (ImGui::TreeNode(sub.name.c_str()))
            {
                for (auto sub_sub : sub.childrens)
                {
                    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                    if (sub_sub.data)
                    {
                        AddFilter(sub_sub.data);
                    }
                    else if (ImGui::TreeNode(sub_sub.name.c_str()))
                    {
                        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                        for (auto end : sub_sub.childrens)
                        {
                            if (!end.data)
                                continue;
                            else
                            {
                                AddFilter(end.data);
                            }
                        }   
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::PopStyleColor();
    }
}

/****************************************************************************************
 * 
 * Media Output window
 *
 ***************************************************************************************/
static void ShowMediaOutputWindow(ImDrawList *draw_list)
{
    ImVec2 window_pos = ImGui::GetWindowPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    const ImVec2 item_size(window_size.x, 32);
    ImGui::SetWindowFontScale(2.5);
    ImGui::PushStyleVar(ImGuiStyleVar_TexGlyphOutlineWidth, 0.5f);
    ImGui::PushStyleColor(ImGuiCol_TexGlyphOutline, ImVec4(0.2, 0.2, 0.2, 0.7));
    draw_list->AddText(window_pos + ImVec2(8, 0), IM_COL32(56, 56, 56, 128), "Media");
    draw_list->AddText(window_pos + ImVec2(8, 48), IM_COL32(56, 56, 56, 128), "Output");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::SetWindowFontScale(1.0);
    if (!timeline)
        return;

    ImGui::Dummy(ImVec2(0, 20));
    ImGui::TextUnformatted("File Name:"); ImGui::SameLine(0.f, 10.f);
    string value = timeline->mOutputName;
    if (ImGui::InputText("##output_file_name_string_value", (char*)value.data(), value.size() + 1, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData* data) -> int
    {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
        {
            auto& stringValue = *static_cast<string*>(data->UserData);
            ImVector<char>* my_str = (ImVector<char>*)data->UserData;
            IM_ASSERT(stringValue.data() == data->Buf);
            stringValue.resize(data->BufSize);
            data->Buf = (char*)stringValue.data();
        }
        else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
        {
            auto& stringValue = *static_cast<string*>(data->UserData);
            stringValue = std::string(data->Buf);
        }
        return 0;
    }, &value))
    {
        value.resize(strlen(value.c_str()));
        if (timeline->mOutputName.compare(value) != 0)
        {
            timeline->mOutputName = value;
        }
    }
    ImGui::TextUnformatted("File Path:"); ImGui::SameLine(0.f, 10.f);
    value = timeline->mOutputPath;
    if (ImGui::InputText("##output_file_path_string_value", (char*)value.data(), value.size() + 1, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData* data) -> int
    {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
        {
            auto& stringValue = *static_cast<string*>(data->UserData);
            ImVector<char>* my_str = (ImVector<char>*)data->UserData;
            IM_ASSERT(stringValue.data() == data->Buf);
            stringValue.resize(data->BufSize);
            data->Buf = (char*)stringValue.data();
        }
        else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
        {
            auto& stringValue = *static_cast<string*>(data->UserData);
            stringValue = std::string(data->Buf);
        }
        return 0;
    }, &value))
    {
        value.resize(strlen(value.c_str()));
        if (timeline->mOutputPath.compare(value) != 0)
        {
            timeline->mOutputPath = value;
        }
    }
    if (ImGui::IsItemHovered() && !timeline->mOutputPath.empty())
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(timeline->mOutputPath.c_str());
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    if (ImGui::Button("...##output_path_browse"))
    {
        ImGuiFileDialog::Instance()->OpenModal("##MediaEditOutputPathDlgKey", ICON_IGFD_FOLDER_OPEN " Output Path", 
                                                    nullptr,
                                                    timeline->mOutputPath.empty() ? "." : timeline->mOutputPath,
                                                    1, 
                                                    IGFDUserDatas("OutputPath"), 
                                                    ImGuiFileDialogFlags_ShowBookmark);
    }

    // Video Setting
    ImGui::Dummy(ImVec2(0, 20));
    ImGui::Checkbox("Export Video##export_video", &timeline->bExportVideo);
    ImGui::Separator();
    if (timeline->bExportVideo) ImGui::BeginDisabled(false); else ImGui::BeginDisabled(true);
    // TODO::Dicky add video encode setting
    ImGui::EndDisabled();
    ImGui::Separator();

    // Audio Setting
    ImGui::Dummy(ImVec2(0, 20));
    ImGui::Checkbox("Export Audio##export_audio", &timeline->bExportAudio);
    ImGui::Separator();
    if (timeline->bExportAudio) ImGui::BeginDisabled(false); else ImGui::BeginDisabled(true);
    // TODO::Dicky add audio encode setting
    ImGui::EndDisabled();
    ImGui::Separator();

    // File dialog
    ImVec2 minSize = ImVec2(600, 600);
	ImVec2 maxSize = ImVec2(FLT_MAX, FLT_MAX);
    if (ImGuiFileDialog::Instance()->Display("##MediaEditOutputPathDlgKey", ImGuiWindowFlags_NoCollapse, minSize, maxSize))
    {
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            auto file_path = ImGuiFileDialog::Instance()->GetFilePathName();
            timeline->mOutputPath = file_path;
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

/****************************************************************************************
 * 
 * Media Preview window
 *
 ***************************************************************************************/
static void ShowMediaPreviewWindow(ImDrawList *draw_list)
{
    // preview control pannel
    ImVec2 PanelBarPos;
    ImVec2 PanelBarSize;
    ImVec2 window_pos = ImGui::GetCursorScreenPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    draw_list->AddRectFilled(window_pos, window_pos + window_size, COL_DEEP_DARK);
    PanelBarPos = window_pos + window_size - ImVec2(window_size.x, 48);
    PanelBarSize = ImVec2(window_size.x, 48);
    draw_list->AddRectFilled(PanelBarPos, PanelBarPos + PanelBarSize, COL_DARK_PANEL);

    // Preview buttons Stop button is center of Panel bar
    auto PanelCenterX = PanelBarPos.x + window_size.x / 2;
    auto PanelButtonY = PanelBarPos.y + 8;

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.5));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2, 0.2, 0.2, 1.0));

    ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16 - 8 - 32 - 8 - 32 - 8 - 32, PanelButtonY));
    if (ImGui::Button(ICON_TO_START "##preview_tostart", ImVec2(32, 32)))
    {
        if (timeline)
            timeline->ToStart();
    }
    ImGui::ShowTooltipOnHover("To Start");

    ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16 - 8 - 32 - 8 - 32, PanelButtonY));
    if (ImGui::Button(ICON_STEP_BACKWARD "##preview_step_backward", ImVec2(32, 32)))
    {
        if (timeline)
            timeline->Step(false);
    }
    ImGui::ShowTooltipOnHover("Step Prev");

    ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16 - 8 - 32, PanelButtonY));
    if (ImGui::Button(ICON_FAST_BACKWARD "##preview_reverse", ImVec2(32, 32)))
    {
        if (timeline)
            timeline->Play(true, false);
    }
    ImGui::ShowTooltipOnHover("Reverse");

    ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16, PanelButtonY));
    if (ImGui::Button(ICON_STOP "##preview_stop", ImVec2(32, 32)))
    {
        if (timeline)
            timeline->Play(false, true);
    }
    ImGui::ShowTooltipOnHover("Stop");

    ImGui::SetCursorScreenPos(ImVec2(PanelCenterX + 16 + 8, PanelButtonY));
    if (ImGui::Button(ICON_FAST_FORWARD "##preview_play", ImVec2(32, 32)))
    {
        if (timeline)
            timeline->Play(true, true);
    }
    ImGui::ShowTooltipOnHover("Play");

    ImGui::SetCursorScreenPos(ImVec2(PanelCenterX + 16 + 8 + 32 + 8, PanelButtonY));
    if (ImGui::Button(ICON_STEP_FORWARD "##preview_step_forward", ImVec2(32, 32)))
    {
        if (timeline)
            timeline->Step(true);
    }
    ImGui::ShowTooltipOnHover("Step Next");

    ImGui::SetCursorScreenPos(ImVec2(PanelCenterX + 16 + 8 + 32 + 8 + 32 + 8, PanelButtonY));
    if (ImGui::Button(ICON_TO_END "##preview_toend", ImVec2(32, 32)))
    {
        if (timeline)
            timeline->ToEnd();
    }
    ImGui::ShowTooltipOnHover("To End");

    bool loop = timeline ? timeline->bLoop : false;
    ImGui::SetCursorScreenPos(ImVec2(PanelCenterX + 16 + 8 + 32 + 8 + 32 + 8 + 32 + 8, PanelButtonY));
    if (ImGui::Button(loop ? ICON_LOOP : ICON_LOOP_ONE "##preview_loop", ImVec2(32, 32)))
    {
        if (timeline)
        {
            loop = !loop;
            timeline->Loop(loop);
        }
    }
    ImGui::ShowTooltipOnHover("Loop");

    // Time stamp on left of control panel
    auto PanelRightX = PanelBarPos.x + window_size.x - 150;
    auto PanelRightY = PanelBarPos.y + 8;
    auto time_str = TimelineMillisecToString(timeline->currentTime, 3);
    ImGui::SetWindowFontScale(1.5);
    draw_list->AddText(ImVec2(PanelRightX, PanelRightY), timeline->mIsPreviewPlaying ? COL_MARK : COL_MARK_HALF, time_str.c_str());
    ImGui::SetWindowFontScale(1.0);

    // audio meters
    ImVec2 AudioMeterPos;
    ImVec2 AudioMeterSize;
    AudioMeterPos = window_pos + ImVec2(window_size.x - 70, 16);
    AudioMeterSize = ImVec2(32, window_size.y - 48 - 16 - 8);
    ImVec2 AudioUVLeftPos = AudioMeterPos + ImVec2(36, 0);
    ImVec2 AudioUVLeftSize = ImVec2(12, AudioMeterSize.y);
    ImVec2 AudioUVRightPos = AudioMeterPos + ImVec2(36 + 16, 0);
    ImVec2 AudioUVRightSize = AudioUVLeftSize;

    draw_list->AddRectFilled(AudioMeterPos - ImVec2(0, 16), AudioMeterPos + ImVec2(70, AudioMeterSize.y + 8), COL_DARK_TWO);

    for (int i = 0; i <= 96; i+= 5)
    {
        float mark_step = AudioMeterSize.y / 96.0f;
        ImVec2 MarkPos = AudioMeterPos + ImVec2(0, i * mark_step);
        if (i % 10 == 0)
        {
            std::string mark_str = i == 0 ? "  0" : "-" + std::to_string(i);
            draw_list->AddLine(MarkPos + ImVec2(20, 8), MarkPos + ImVec2(30, 8), COL_MARK_HALF, 1);
            ImGui::SetWindowFontScale(0.75);
            draw_list->AddText(MarkPos + ImVec2(0, 2), COL_MARK_HALF, mark_str.c_str());
            ImGui::SetWindowFontScale(1.0);
        }
        else
        {
            draw_list->AddLine(MarkPos + ImVec2(25, 8), MarkPos + ImVec2(30, 8), COL_MARK_HALF, 1);
        }
    }

    static int left_stack = 0;
    static int left_count = 0;
    static int right_stack = 0;
    static int right_count = 0;
    int l_level = timeline->GetAudioLevel(0);
    int r_level = timeline->GetAudioLevel(1);
    ImGui::SetCursorScreenPos(AudioUVLeftPos);
    ImGui::UvMeter("##luv", AudioUVLeftSize, &l_level, 0, 96, AudioUVLeftSize.y / 4, &left_stack, &left_count);
    ImGui::SetCursorScreenPos(AudioUVRightPos);
    ImGui::UvMeter("##ruv", AudioUVRightSize, &r_level, 0, 96, AudioUVRightSize.y / 4, &right_stack, &right_count);

    // video texture area
    ImVec2 PreviewPos;
    ImVec2 PreviewSize;
    PreviewPos = window_pos + ImVec2(8, 8);
    PreviewSize = window_size - ImVec2(16 + 64, 16 + 48);

    auto frame = timeline->GetPreviewFrame();
    if (!frame.empty())
    {
#if IMGUI_VULKAN_SHADER
        if (m_histogram) m_histogram->scope(frame, mat_histogram, 256, g_media_editor_settings.HistogramScale, g_media_editor_settings.HistogramLog);
        if (m_waveform) m_waveform->scope(frame, mat_waveform, 256, g_media_editor_settings.WaveformIntensity, g_media_editor_settings.WaveformSeparate);
        if (m_cie) m_cie->scope(frame, mat_cie, g_media_editor_settings.CIEIntensity, g_media_editor_settings.CIEShowColor);
        if (m_vector) m_vector->scope(frame, mat_vector, g_media_editor_settings.VectorIntensity);
#endif
        ImGui::ImMatToTexture(frame, timeline->mMainPreviewTexture);
    }
    ShowVideoWindow(timeline->mMainPreviewTexture, PreviewPos, PreviewSize);

    // Show monitors
    MonitorButton("preview_monitor_select", ImVec2(PanelBarPos.x + 20, PanelBarPos.y + 16), MonitorIndexPreviewVideo);

    ImGui::PopStyleColor(3);

    ImGui::SetCursorScreenPos(window_pos + ImVec2(40, 30));
    ImGui::TextComplex("Preview", 2.0f, ImVec4(0.8, 0.8, 0.8, 0.2),
                        0.1f, ImVec4(0.8, 0.8, 0.8, 0.3),
                        ImVec2(4, 4), ImVec4(0.0, 0.0, 0.0, 0.5));
}

/****************************************************************************************
 * 
 * Video Editor windows
 *
 ***************************************************************************************/
static void ShowVideoCropWindow(ImDrawList *draw_list)
{
    ImGui::SetWindowFontScale(1.2);
    ImGui::Indent(20);
    ImGui::PushStyleVar(ImGuiStyleVar_TexGlyphOutlineWidth, 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4, 0.4, 0.8, 0.8));
    ImGui::TextUnformatted("Video Crop");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::SetWindowFontScale(1.0);
}

static void ShowVideoRotateWindow(ImDrawList *draw_list)
{
    ImGui::SetWindowFontScale(1.2);
    ImGui::Indent(20);
    ImGui::PushStyleVar(ImGuiStyleVar_TexGlyphOutlineWidth, 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4, 0.4, 0.8, 0.8));
    ImGui::TextUnformatted("Video Rotate");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::SetWindowFontScale(1.0);
}

/****************************************************************************************
 * 
 * Video Filter window
 *
 ***************************************************************************************/
static void ShowVideoFilterBluePrintWindow(ImDrawList *draw_list, Clip * clip)
{
    if (timeline && timeline->mVideoFilterBluePrint)
    {
        if (clip && !timeline->mVideoFilterBluePrint->m_Document->m_Blueprint.IsOpened())
        {
            auto track = timeline->FindTrackByClipID(clip->mID);
            if (track)
                track->SelectEditingClip(clip);
            timeline->mVideoFilterBluePrint->View_ZoomToContent();
        }
        ImVec2 window_pos = ImGui::GetCursorScreenPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        ImGui::SetCursorScreenPos(window_pos + ImVec2(3, 3));
        ImGui::InvisibleButton("video_editor_blueprint_back_view", window_size - ImVec2(6, 6));
        if (ImGui::BeginDragDropTarget() && timeline->mVideoFilterBluePrint->Blueprint_IsValid())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Filter_drag_drop_Video"))
            {
                BluePrint::NodeTypeInfo * type = (BluePrint::NodeTypeInfo *)payload->Data;
                if (type)
                {
                    timeline->mVideoFilterBluePrint->Edit_Insert(type->m_ID);
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SetCursorScreenPos(window_pos + ImVec2(1, 1));
        if (ImGui::BeginChild("##video_editor_blueprint", window_size - ImVec2(2, 2), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            timeline->mVideoFilterBluePrint->Frame(true, true, clip != nullptr, BluePrint::BluePrintFlag::BluePrintFlag_Filter);
        }
        ImGui::EndChild();
    }
}

static void ShowVideoFilterWindow(ImDrawList *draw_list)
{
    ImVec2 window_pos = ImGui::GetCursorScreenPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    draw_list->AddRectFilled(window_pos, window_pos + window_size, COL_DEEP_DARK);
    float clip_timeline_height = 80;
    float editor_main_height = window_size.y - clip_timeline_height - 4;
    float video_view_width = window_size.x * 2 / 3;
    float video_editor_width = window_size.x - video_view_width;
    
    if (!timeline)
        return;
    
    Clip * editing_clip = timeline->FindEditingClip();
    if (editing_clip && editing_clip->mType != MEDIA_VIDEO)
    {
        editing_clip = nullptr;
    }

    if (editing_clip && timeline->mVidFilterClip)
    {
        timeline->mVidFilterClip->UpdateClipRange(editing_clip);
    }

    if (editing_clip && timeline->mVideoFilterBluePrint)
    {
        timeline->mVideoFilterBluePrint->m_ViewSize = ImVec2(video_editor_width, editor_main_height);
    }

    if (ImGui::BeginChild("##video_filter_main", ImVec2(window_size.x, editor_main_height), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 clip_window_pos = ImGui::GetCursorScreenPos();
        ImVec2 clip_window_size = ImGui::GetWindowSize();
        if (ImGui::BeginChild("##filter_video_view", ImVec2(video_view_width, clip_window_size.y), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            ImVec2 video_view_window_pos = ImGui::GetCursorScreenPos();
            ImVec2 video_view_window_size = ImGui::GetWindowSize();
            draw_list->AddRectFilled(video_view_window_pos, video_view_window_pos + video_view_window_size, COL_DEEP_DARK);
            // Draw Video Filter Play control bar
            ImVec2 PanelBarPos = video_view_window_pos + ImVec2(0, (video_view_window_size.y - 36));
            ImVec2 PanelBarSize = ImVec2(video_view_window_size.x, 36);
            draw_list->AddRectFilled(PanelBarPos, PanelBarPos + PanelBarSize, COL_DARK_PANEL);
            // Preview buttons Stop button is center of Panel bar
            auto PanelCenterX = PanelBarPos.x + video_view_window_size.x / 2;
            auto PanelButtonY = PanelBarPos.y + 2;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.5));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2, 0.2, 0.2, 1.0));
            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16 - 8 - 32 - 8 - 32 - 8 - 32, PanelButtonY));
            if (ImGui::Button(ICON_TO_START "##video_filter_tostart", ImVec2(32, 32)))
            {
                if (timeline->mVidFilterClip && !timeline->mVidFilterClip->bPlay)
                {
                    int64_t pos = timeline->mVidFilterClip->mStartOffset;
                    timeline->mVidFilterClip->Seek(pos);
                }
            }
            ImGui::ShowTooltipOnHover("To Start");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16 - 8 - 32 - 8 - 32, PanelButtonY));
            if (ImGui::Button(ICON_STEP_BACKWARD "##video_filter_step_backward", ImVec2(32, 32)))
            {
                if (timeline->mVidFilterClip)
                {
                    timeline->mVidFilterClip->Step(false);
                }
            }
            ImGui::ShowTooltipOnHover("Step Prev");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16 - 8 - 32, PanelButtonY));
            if (ImGui::Button(ICON_FAST_BACKWARD "##video_filter_reverse", ImVec2(32, 32)))
            {
                if (timeline->mVidFilterClip)
                {
                    timeline->mVidFilterClip->bForward = false;
                    timeline->mVidFilterClip->bPlay = true;
                }
            }
            ImGui::ShowTooltipOnHover("Reverse");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16, PanelButtonY));
            if (ImGui::Button(ICON_STOP "##video_filter_stop", ImVec2(32, 32)))
            {
                if (timeline->mVidFilterClip)
                {
                    timeline->mVidFilterClip->bPlay = false;
                    timeline->mVidFilterClip->mLastTime = -1;
                }
            }
            ImGui::ShowTooltipOnHover("Stop");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX + 16 + 8, PanelButtonY));
            if (ImGui::Button(ICON_FAST_FORWARD "##video_filter_play", ImVec2(32, 32)))
            {
                if (timeline->mVidFilterClip)
                {
                    timeline->mVidFilterClip->bForward = true;
                    timeline->mVidFilterClip->bPlay = true;
                }
            }
            ImGui::ShowTooltipOnHover("Play");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX + 16 + 8 + 32 + 8, PanelButtonY));
            if (ImGui::Button(ICON_STEP_FORWARD "##video_filter_step_forward", ImVec2(32, 32)))
            {
                if (timeline->mVidFilterClip)
                {
                    timeline->mVidFilterClip->Step(true);
                }
            }
            ImGui::ShowTooltipOnHover("Step Next");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX + 16 + 8 + 32 + 8 + 32 + 8, PanelButtonY));
            if (ImGui::Button(ICON_TO_END "##video_filter_toend", ImVec2(32, 32)))
            {
                if (timeline->mVidFilterClip && !timeline->mVidFilterClip->bPlay)
                {
                    int64_t pos = timeline->mVidFilterClip->mEnd - timeline->mVidFilterClip->mStart + timeline->mVidFilterClip->mStartOffset;
                    timeline->mVidFilterClip->Seek(pos);
                }
            }
            ImGui::ShowTooltipOnHover("To End");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX + 16 + 8 + 32 + 8 + 32 + 8 + 32 + 8, PanelButtonY + 6));
            ImGui::CheckButton(ICON_COMPARE "##video_filter_compare", &timeline->bCompare);
            ImGui::ShowTooltipOnHover("Compare");

            // filter input texture area
            ImVec2 InputVideoPos;
            ImVec2 InputVideoSize;
            InputVideoPos = video_view_window_pos + ImVec2(4, 4);
            InputVideoSize = ImVec2(video_view_window_size.x - 8, (video_view_window_size.y - PanelBarSize.y - 8) / 3);
            ImVec2 OutputVideoPos;
            ImVec2 OutputVideoSize;
            OutputVideoPos = video_view_window_pos + ImVec2(4, 4 + InputVideoSize.y + 4);
            OutputVideoSize = ImVec2(video_view_window_size.x - 8, (video_view_window_size.y - PanelBarSize.y - 8) * 2 / 3);
            ImRect InputVideoRect(InputVideoPos,InputVideoPos + InputVideoSize);
            ImRect OutVideoRect(OutputVideoPos,OutputVideoPos + OutputVideoSize);
            ImVec2 VideoZoomPos = window_pos + ImVec2(0, editor_main_height - PanelBarSize.y + 4);
            if (timeline->mVidFilterClip)
            {
                ImGuiIO& io = ImGui::GetIO();
                static float texture_zoom = 1.0f;
                if (InputVideoRect.Contains(io.MousePos) || OutVideoRect.Contains(io.MousePos))
                {
                    if (io.MouseWheel < -FLT_EPSILON)
                    {
                        texture_zoom *= 0.9;
                        if (texture_zoom < 0.5) texture_zoom = 0.5;
                    }
                    else if (io.MouseWheel > FLT_EPSILON)
                    {
                        texture_zoom *= 1.1;
                        if (texture_zoom > 4.0) texture_zoom = 4.0;
                    }
                }
                float region_sz = 360.0f / texture_zoom;
                std::pair<ImGui::ImMat, ImGui::ImMat> pair;
                auto ret = timeline->mVidFilterClip->GetFrame(pair);
                if (ret)
                {
#if IMGUI_VULKAN_SHADER
                    if (m_histogram) m_histogram->scope(pair.second, mat_histogram, 256, g_media_editor_settings.HistogramScale, g_media_editor_settings.HistogramLog);
                    if (m_waveform) m_waveform->scope(pair.second, mat_waveform, 256, g_media_editor_settings.WaveformIntensity, g_media_editor_settings.WaveformSeparate);
                    if (m_cie) m_cie->scope(pair.second, mat_cie, g_media_editor_settings.CIEIntensity, g_media_editor_settings.CIEShowColor);
                    if (m_vector) m_vector->scope(pair.second, mat_vector, g_media_editor_settings.VectorIntensity);
#endif
                    ImGui::ImMatToTexture(pair.first, timeline->mVideoFilterInputTexture);
                    ImGui::ImMatToTexture(pair.second, timeline->mVideoFilterOutputTexture);
                }
                float pos_x = 0, pos_y = 0;
                bool draw_compare = false;
                ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);   // No tint
                ImVec4 border_col = ImVec4(1.0f, 1.0f, 1.0f, 0.5f); // 50% opaque white

                float offset_x = 0, offset_y = 0;
                float tf_x = 0, tf_y = 0;
                // filter input texture area
                ShowVideoWindow(timeline->mVideoFilterInputTexture, InputVideoPos, InputVideoSize, offset_x, offset_y, tf_x, tf_y);
                if (ImGui::IsItemHovered() && timeline->mVideoFilterInputTexture)
                {
                    float image_width = ImGui::ImGetTextureWidth(timeline->mVideoFilterInputTexture);
                    float image_height = ImGui::ImGetTextureHeight(timeline->mVideoFilterInputTexture);
                    float scale_w = image_width / (InputVideoSize.x - tf_x * 2);
                    float scale_h = image_height / (InputVideoSize.y - tf_y * 2);
                    pos_x = (io.MousePos.x - offset_x) * scale_w;
                    pos_y = (io.MousePos.y - offset_y) * scale_h;
                    draw_compare = true;
                }
                // filter output texture area
                ShowVideoWindow(timeline->mVideoFilterOutputTexture, OutputVideoPos, OutputVideoSize, offset_x, offset_y, tf_x, tf_y);
                if (ImGui::IsItemHovered() && timeline->mVideoFilterOutputTexture)
                {
                    float image_width = ImGui::ImGetTextureWidth(timeline->mVideoFilterOutputTexture);
                    float image_height = ImGui::ImGetTextureHeight(timeline->mVideoFilterOutputTexture);
                    float scale_w = image_width / (OutputVideoSize.x - tf_x * 2);
                    float scale_h = image_height / (OutputVideoSize.y - tf_y * 2);
                    pos_x = (io.MousePos.x - offset_x) * scale_w;
                    pos_y = (io.MousePos.y - offset_y) * scale_h;
                    draw_compare = true;
                }
                if (timeline->bCompare && draw_compare)
                {
                    if (timeline->mVideoFilterInputTexture)
                    {
                        float image_width = ImGui::ImGetTextureWidth(timeline->mVideoFilterInputTexture);
                        float image_height = ImGui::ImGetTextureHeight(timeline->mVideoFilterInputTexture);
                        float region_x = pos_x - region_sz * 0.5f;
                        float region_y = pos_y - region_sz * 0.5f;
                        if (region_x < 0.0f) { region_x = 0.0f; }
                        else if (region_x > image_width - region_sz) { region_x = image_width - region_sz; }
                        if (region_y < 0.0f) { region_y = 0.0f; }
                        else if (region_y > image_height - region_sz) { region_y = image_height - region_sz; }
                        ImGui::SetNextWindowPos(VideoZoomPos);
                        ImGui::SetNextWindowBgAlpha(1.0);
                        ImGui::BeginTooltip();
                        ImVec2 uv0 = ImVec2((region_x) / image_width, (region_y) / image_height);
                        ImVec2 uv1 = ImVec2((region_x + region_sz) / image_width, (region_y + region_sz) / image_height);
                        ImGui::Image(timeline->mVideoFilterInputTexture, ImVec2(region_sz * texture_zoom, region_sz * texture_zoom), uv0, uv1, tint_col, border_col);
                        ImGui::EndTooltip();
                    }
                    if (timeline->mVideoFilterOutputTexture)
                    {
                        float image_width = ImGui::ImGetTextureWidth(timeline->mVideoFilterOutputTexture);
                        float image_height = ImGui::ImGetTextureHeight(timeline->mVideoFilterOutputTexture);
                        float region_x = pos_x - region_sz * 0.5f;
                        float region_y = pos_y - region_sz * 0.5f;
                        if (region_x < 0.0f) { region_x = 0.0f; }
                        else if (region_x > image_width - region_sz) { region_x = image_width - region_sz; }
                        if (region_y < 0.0f) { region_y = 0.0f; }
                        else if (region_y > image_height - region_sz) { region_y = image_height - region_sz; }
                        ImGui::SetNextWindowBgAlpha(1.0);
                        ImGui::BeginTooltip();
                        ImGui::SameLine();
                        ImVec2 uv0 = ImVec2((region_x) / image_width, (region_y) / image_height);
                        ImVec2 uv1 = ImVec2((region_x + region_sz) / image_width, (region_y + region_sz) / image_height);
                        ImGui::Image(timeline->mVideoFilterOutputTexture, ImVec2(region_sz * texture_zoom, region_sz * texture_zoom), uv0, uv1, tint_col, border_col);
                        ImGui::EndTooltip();
                    }
                }
                // Show monitors
                MonitorButton("video_filter_org_monitor_select", ImVec2(PanelBarPos.x + 20, PanelBarPos.y + 8), MonitorIndexVideoFilterOrg, MonitorIndexVideoFiltered);
                MonitorButton("video_filter_monitor_select", ImVec2(PanelBarPos.x + PanelBarSize.x - 100, PanelBarPos.y + 8), MonitorIndexVideoFiltered, MonitorIndexVideoFilterOrg);
            }
            ImGui::PopStyleColor(3);
        }
        ImGui::EndChild();
        ImGui::SetCursorScreenPos(clip_window_pos + ImVec2(video_view_width, 0));
        if (ImGui::BeginChild("##video_filter_blueprint", ImVec2(video_editor_width, clip_window_size.y), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            ImVec2 editor_view_window_pos = ImGui::GetCursorScreenPos();
            ImVec2 editor_view_window_size = ImGui::GetWindowSize();
            draw_list->AddRectFilled(editor_view_window_pos, editor_view_window_pos + editor_view_window_size, COL_DARK_ONE);
            ShowVideoFilterBluePrintWindow(draw_list, editing_clip);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("##video_filter_timeline", ImVec2(window_size.x, clip_timeline_height), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 clip_timeline_window_pos = ImGui::GetCursorScreenPos();
        ImVec2 clip_timeline_window_size = ImGui::GetWindowSize();
        draw_list->AddRectFilled(clip_timeline_window_pos, clip_timeline_window_pos + clip_timeline_window_size, COL_DARK_TWO);

        // Draw Clip TimeLine
        DrawClipTimeLine(timeline->mVidFilterClip);
    }
    ImGui::EndChild();
}

/****************************************************************************************
 * 
 * Video Fusion window
 *
 ***************************************************************************************/
static void ShowVideoFusionBluePrintWindow(ImDrawList *draw_list, Overlap * overlap)
{
    if (timeline && timeline->mVideoFusionBluePrint)
    {
        if (overlap && !timeline->mVideoFusionBluePrint->m_Document->m_Blueprint.IsOpened())
        {
            auto track = timeline->FindTrackByClipID(overlap->m_Clip.first);
            if (track)
                track->EditingOverlap(overlap);
            timeline->mVideoFusionBluePrint->View_ZoomToContent();
        }
        ImVec2 window_pos = ImGui::GetCursorScreenPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        ImGui::SetCursorScreenPos(window_pos + ImVec2(3, 3));
        ImGui::InvisibleButton("video_fusion_blueprint_back_view", window_size - ImVec2(6, 6));
        if (ImGui::BeginDragDropTarget() && timeline->mVideoFusionBluePrint->Blueprint_IsValid())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Fusion_drag_drop_Video"))
            {
                BluePrint::NodeTypeInfo * type = (BluePrint::NodeTypeInfo *)payload->Data;
                if (type)
                {
                    timeline->mVideoFusionBluePrint->Edit_Insert(type->m_ID);
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SetCursorScreenPos(window_pos + ImVec2(1, 1));
        if (ImGui::BeginChild("##fusion_edit_blueprint", window_size - ImVec2(2, 2), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            timeline->mVideoFusionBluePrint->Frame(true, true, overlap != nullptr, BluePrint::BluePrintFlag::BluePrintFlag_Fusion);
        }
        ImGui::EndChild();
    }
}

static void ShowVideoFusionWindow(ImDrawList *draw_list)
{
    ImVec2 window_pos = ImGui::GetCursorScreenPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    draw_list->AddRectFilled(window_pos, window_pos + window_size, COL_DEEP_DARK);
    float fusion_timeline_height = 130;
    float fusion_main_height = window_size.y - fusion_timeline_height - 4;
    float video_view_width = window_size.x * 2 / 3;
    float video_fusion_width = window_size.x - video_view_width;
    if (!timeline)
        return;
    
    Overlap * editing_overlap = timeline->FindEditingOverlap();

    if (editing_overlap)
    {
        auto clip_first = timeline->FindClipByID(editing_overlap->m_Clip.first);
        auto clip_second = timeline->FindClipByID(editing_overlap->m_Clip.second);
        if (!clip_first || !clip_second || 
            clip_first->mType != MEDIA_VIDEO || clip_second->mType != MEDIA_VIDEO)
        {
            editing_overlap = nullptr;
        }
    }

    if (editing_overlap && timeline->mVideoFusionBluePrint)
    {
        timeline->mVideoFusionBluePrint->m_ViewSize = ImVec2(video_fusion_width, fusion_main_height);
    }
    
    if (ImGui::BeginChild("##video_fusion_main", ImVec2(window_size.x, fusion_main_height), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 fusion_window_pos = ImGui::GetCursorScreenPos();
        ImVec2 fusion_window_size = ImGui::GetWindowSize();
        if (ImGui::BeginChild("##fusion_video_view", ImVec2(video_view_width, fusion_window_size.y), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            ImVec2 video_view_window_pos = ImGui::GetCursorScreenPos();
            ImVec2 video_view_window_size = ImGui::GetWindowSize();
            draw_list->AddRectFilled(video_view_window_pos, video_view_window_pos + video_view_window_size, COL_DEEP_DARK);

            // Draw Video Fusion Play control bar
            ImVec2 PanelBarPos = video_view_window_pos + ImVec2(0, (video_view_window_size.y - 36));
            ImVec2 PanelBarSize = ImVec2(video_view_window_size.x, 36);
            draw_list->AddRectFilled(PanelBarPos, PanelBarPos + PanelBarSize, COL_DARK_PANEL);
            
            // Preview buttons Stop button is center of Panel bar
            auto PanelCenterX = PanelBarPos.x + video_view_window_size.x / 2;
            auto PanelButtonY = PanelBarPos.y + 2;
        
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.5));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2, 0.2, 0.2, 1.0));

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16 - 8 - 32 - 8 - 32 - 8 - 32, PanelButtonY));
            if (ImGui::Button(ICON_TO_START "##video_fusion_tostart", ImVec2(32, 32)))
            {
                // TODO::Dicky
            }
            ImGui::ShowTooltipOnHover("To Start");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16 - 8 - 32 - 8 - 32, PanelButtonY));
            if (ImGui::Button(ICON_STEP_BACKWARD "##video_fusion_step_backward", ImVec2(32, 32)))
            {
                // TODO::Dicky
            }
            ImGui::ShowTooltipOnHover("Step Prev");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16 - 8 - 32, PanelButtonY));
            if (ImGui::Button(ICON_FAST_BACKWARD "##video_fusion_reverse", ImVec2(32, 32)))
            {
                // TODO::Dicky
            }
            ImGui::ShowTooltipOnHover("Reverse");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX - 16, PanelButtonY));
            if (ImGui::Button(ICON_STOP "##video_fusion_stop", ImVec2(32, 32)))
            {
                // TODO::Dicky
            }
            ImGui::ShowTooltipOnHover("Stop");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX + 16 + 8, PanelButtonY));
            if (ImGui::Button(ICON_FAST_FORWARD "##video_fusion_play", ImVec2(32, 32)))
            {
                // TODO::Dicky
            }
            ImGui::ShowTooltipOnHover("Play");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX + 16 + 8 + 32 + 8, PanelButtonY));
            if (ImGui::Button(ICON_STEP_FORWARD "##video_fusion_step_forward", ImVec2(32, 32)))
            {
                // TODO::Dicky
            }
            ImGui::ShowTooltipOnHover("Step Next");

            ImGui::SetCursorScreenPos(ImVec2(PanelCenterX + 16 + 8 + 32 + 8 + 32 + 8, PanelButtonY));
            if (ImGui::Button(ICON_TO_END "##video_fusion_toend", ImVec2(32, 32)))
            {
                // TODO::Dicky
            }
            ImGui::ShowTooltipOnHover("To End");

            ImGui::PopStyleColor(3);
        }
        ImGui::EndChild();

        ImGui::SetCursorScreenPos(fusion_window_pos + ImVec2(video_view_width, 0));
        if (ImGui::BeginChild("##video_fusion_blueprint_view", ImVec2(video_fusion_width, fusion_window_size.y), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            ImVec2 fusion_view_window_pos = ImGui::GetCursorScreenPos();
            ImVec2 fusion_view_window_size = ImGui::GetWindowSize();
            draw_list->AddRectFilled(fusion_view_window_pos, fusion_view_window_pos + fusion_view_window_size, COL_DEEP_DARK);
            ShowVideoFusionBluePrintWindow(draw_list, editing_overlap);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("##video_fusion_timeline", ImVec2(window_size.x, fusion_timeline_height), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 clip_timeline_window_pos = ImGui::GetCursorScreenPos();
        ImVec2 clip_timeline_window_size = ImGui::GetWindowSize();
        draw_list->AddRectFilled(clip_timeline_window_pos, clip_timeline_window_pos + clip_timeline_window_size, COL_DARK_TWO);

        // Draw Clip TimeLine
        DrawOverlapTimeLine(editing_overlap);
    }
    ImGui::EndChild();
}

static void ShowVideoEditorWindow(ImDrawList *draw_list)
{
    float labelWidth = ImGui::CalcVerticalTabLabelsWidth() + 4;
    ImVec2 clip_window_pos = ImGui::GetCursorScreenPos();
    ImVec2 clip_window_size = ImGui::GetWindowSize();
    static const int numTabs = sizeof(VideoEditorTabNames)/sizeof(VideoEditorTabNames[0]);
    if (ImGui::TabLabelsVertical(false, numTabs, VideoEditorTabNames, VideoEditorWindowIndex, VideoEditorTabTooltips, true, nullptr, nullptr, false, false, nullptr, nullptr))
    {
        UIPageChanged();
    }
    ImGui::SetCursorScreenPos(clip_window_pos + ImVec2(labelWidth, 0));
    if (ImGui::BeginChild("##video_editor_views", ImVec2(clip_window_size.x - labelWidth, clip_window_size.y), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 editor_view_window_pos = ImGui::GetCursorScreenPos();
        ImVec2 editor_view_window_size = ImGui::GetWindowSize();
        draw_list->AddRectFilled(editor_view_window_pos, editor_view_window_pos + editor_view_window_size, COL_DARK_ONE);
        switch (VideoEditorWindowIndex)
        {
            case 0: ShowVideoFilterWindow(draw_list); break;
            case 1: ShowVideoFusionWindow(draw_list); break;
            case 2: ShowVideoCropWindow(draw_list); break;
            case 3: ShowVideoRotateWindow(draw_list); break;
            default: break;
        }
    }
    ImGui::EndChild();
}
/****************************************************************************************
 * 
 * Audio Editor windows
 *
 ***************************************************************************************/
static void ShowAudioFilterBluePrintWindow(ImDrawList *draw_list, Clip * clip)
{
    if (timeline && timeline->mAudioFilterBluePrint)
    {
        if (clip && !timeline->mAudioFilterBluePrint->m_Document->m_Blueprint.IsOpened())
        {
            auto track = timeline->FindTrackByClipID(clip->mID);
            if (track)
                track->SelectEditingClip(clip);
            timeline->mAudioFilterBluePrint->View_ZoomToContent();
        }
        ImVec2 window_pos = ImGui::GetCursorScreenPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        ImGui::SetCursorScreenPos(window_pos + ImVec2(3, 3));
        ImGui::InvisibleButton("audio_editor_blueprint_back_view", window_size - ImVec2(6, 6));
        if (ImGui::BeginDragDropTarget() && timeline->mAudioFilterBluePrint->Blueprint_IsValid())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Filter_drag_drop_Audio"))
            {
                BluePrint::NodeTypeInfo * type = (BluePrint::NodeTypeInfo *)payload->Data;
                if (type)
                {
                    timeline->mAudioFilterBluePrint->Edit_Insert(type->m_ID);
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SetCursorScreenPos(window_pos + ImVec2(1, 1));
        if (ImGui::BeginChild("##audio_editor_blueprint", window_size - ImVec2(2, 2), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            timeline->mAudioFilterBluePrint->Frame(true, true, clip != nullptr, BluePrint::BluePrintFlag::BluePrintFlag_Filter);
        }
        ImGui::EndChild();
    }
}

static void ShowAudioFilterWindow(ImDrawList *draw_list)
{
    ImVec2 window_pos = ImGui::GetCursorScreenPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    draw_list->AddRectFilled(window_pos, window_pos + window_size, COL_DEEP_DARK);
    float clip_timeline_height = 80;
    float editor_main_height = window_size.y - clip_timeline_height - 4;
    float audio_view_width = window_size.x * 2 / 3;
    float audio_editor_width = window_size.x - audio_view_width;

    if (!timeline)
        return;

    Clip * editing_clip = timeline->FindEditingClip();
    if (editing_clip && editing_clip->mType != MEDIA_AUDIO)
    {
        editing_clip = nullptr;
    }

    if (editing_clip)
    {
        if (timeline->mAudFilterClipLock.try_lock())
        {
            if (timeline->mAudFilterClip)
                timeline->mAudFilterClip->UpdateClipRange(editing_clip);
            timeline->mAudFilterClipLock.unlock();
        }
    }

    if (editing_clip && timeline->mAudioFilterBluePrint)
    {
        timeline->mAudioFilterBluePrint->m_ViewSize = ImVec2(audio_editor_width, editor_main_height);
    }

    if (ImGui::BeginChild("##audio_filter_main", ImVec2(window_size.x, editor_main_height), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 clip_window_pos = ImGui::GetCursorScreenPos();
        ImVec2 clip_window_size = ImGui::GetWindowSize();
        if (ImGui::BeginChild("##filter_audio_view", ImVec2(audio_view_width, clip_window_size.y), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            ImVec2 audio_view_window_pos = ImGui::GetCursorScreenPos();
            ImVec2 audio_view_window_size = ImGui::GetWindowSize();
            draw_list->AddRectFilled(audio_view_window_pos, audio_view_window_pos + audio_view_window_size, COL_DEEP_DARK);
            // TODO::Dicky add Audio view control
        }
        ImGui::EndChild();
        ImGui::SetCursorScreenPos(clip_window_pos + ImVec2(audio_view_width, 0));
        if (ImGui::BeginChild("##audio_filter_blueprint", ImVec2(audio_editor_width, clip_window_size.y), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            ImVec2 editor_view_window_pos = ImGui::GetCursorScreenPos();
            ImVec2 editor_view_window_size = ImGui::GetWindowSize();
            draw_list->AddRectFilled(editor_view_window_pos, editor_view_window_pos + editor_view_window_size, COL_DARK_ONE);
            ShowAudioFilterBluePrintWindow(draw_list, editing_clip);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("##audio_filter_timeline", ImVec2(window_size.x, clip_timeline_height), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 clip_timeline_window_pos = ImGui::GetCursorScreenPos();
        ImVec2 clip_timeline_window_size = ImGui::GetWindowSize();
        draw_list->AddRectFilled(clip_timeline_window_pos, clip_timeline_window_pos + clip_timeline_window_size, COL_DARK_TWO);

        // Draw Clip TimeLine
        DrawClipTimeLine(timeline->mAudFilterClip);
    }
    ImGui::EndChild();
}

static void ShowAudioFusionBluePrintWindow(ImDrawList *draw_list, Overlap * overlap)
{
    if (timeline && timeline->mAudioFusionBluePrint)
    {
        if (overlap && !timeline->mAudioFusionBluePrint->m_Document->m_Blueprint.IsOpened())
        {
            auto track = timeline->FindTrackByClipID(overlap->m_Clip.first);
            if (track)
                track->EditingOverlap(overlap);
            timeline->mAudioFusionBluePrint->View_ZoomToContent();
        }
        ImVec2 window_pos = ImGui::GetCursorScreenPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        ImGui::SetCursorScreenPos(window_pos + ImVec2(3, 3));
        ImGui::InvisibleButton("audio_fusion_blueprint_back_view", window_size - ImVec2(6, 6));
        if (ImGui::BeginDragDropTarget() && timeline->mAudioFusionBluePrint->Blueprint_IsValid())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Fusion_drag_drop_Video"))
            {
                BluePrint::NodeTypeInfo * type = (BluePrint::NodeTypeInfo *)payload->Data;
                if (type)
                {
                    timeline->mAudioFusionBluePrint->Edit_Insert(type->m_ID);
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SetCursorScreenPos(window_pos + ImVec2(1, 1));
        if (ImGui::BeginChild("##audio_fusion_blueprint", window_size - ImVec2(2, 2), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            timeline->mAudioFusionBluePrint->Frame(true, true, overlap != nullptr, BluePrint::BluePrintFlag::BluePrintFlag_Fusion);
        }
        ImGui::EndChild();
    }
}

static void ShowAudioFusionWindow(ImDrawList *draw_list)
{
    ImVec2 window_pos = ImGui::GetCursorScreenPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    draw_list->AddRectFilled(window_pos, window_pos + window_size, COL_DEEP_DARK);
    float fusion_timeline_height = 130;
    float fusion_main_height = window_size.y - fusion_timeline_height - 4;
    float audio_view_width = window_size.x * 2 / 3;
    float audio_fusion_width = window_size.x - audio_view_width;
    if (!timeline)
        return;

    Overlap * editing_overlap = timeline->FindEditingOverlap();
    if (editing_overlap)
    {
        auto clip_first = timeline->FindClipByID(editing_overlap->m_Clip.first);
        auto clip_second = timeline->FindClipByID(editing_overlap->m_Clip.second);
        if (!clip_first || !clip_second || 
            clip_first->mType != MEDIA_AUDIO || clip_second->mType != MEDIA_AUDIO)
        {
            editing_overlap = nullptr;
        }
    }

    if (editing_overlap && timeline->mAudioFusionBluePrint)
    {
        timeline->mAudioFusionBluePrint->m_ViewSize = ImVec2(audio_fusion_width, fusion_main_height);
    }

    if (ImGui::BeginChild("##audio_fusion_main", ImVec2(window_size.x, fusion_main_height), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 fusion_window_pos = ImGui::GetCursorScreenPos();
        ImVec2 fusion_window_size = ImGui::GetWindowSize();
        
        if (ImGui::BeginChild("##fusion_audio_view", ImVec2(audio_view_width, fusion_window_size.y), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            ImVec2 audio_view_window_pos = ImGui::GetCursorScreenPos();
            ImVec2 audio_view_window_size = ImGui::GetWindowSize();
            draw_list->AddRectFilled(audio_view_window_pos, audio_view_window_pos + audio_view_window_size, COL_DEEP_DARK);
            // TODO::Dicky audio fusion
        }
        ImGui::EndChild();
        ImGui::SetCursorScreenPos(window_pos + ImVec2(audio_view_width, 0));
        if (ImGui::BeginChild("##audio_fusion_views", ImVec2(audio_fusion_width, fusion_window_size.y), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            ImVec2 fusion_view_window_pos = ImGui::GetCursorScreenPos();
            ImVec2 fusion_view_window_size = ImGui::GetWindowSize();
            draw_list->AddRectFilled(fusion_view_window_pos, fusion_view_window_pos + fusion_view_window_size, COL_DARK_ONE);
            ShowAudioFusionBluePrintWindow(draw_list, editing_overlap);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    
    if (ImGui::BeginChild("##audio_fusion_timeline", ImVec2(window_size.x, fusion_timeline_height), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 fusion_timeline_window_pos = ImGui::GetCursorScreenPos();
        ImVec2 fusion_timeline_window_size = ImGui::GetWindowSize();
        draw_list->AddRectFilled(fusion_timeline_window_pos, fusion_timeline_window_pos + fusion_timeline_window_size, COL_DARK_TWO);

        // Draw Clip TimeLine
        DrawOverlapTimeLine(editing_overlap);
    }
    ImGui::EndChild();
}

static void ShowAudioEditorWindow(ImDrawList *draw_list)
{
    float labelWidth = ImGui::CalcVerticalTabLabelsWidth() + 4;
    ImVec2 clip_window_pos = ImGui::GetCursorScreenPos();
    ImVec2 clip_window_size = ImGui::GetWindowSize();
    static const int numTabs = sizeof(AudioEditorTabNames)/sizeof(AudioEditorTabNames[0]);
    if (ImGui::TabLabelsVertical(false, numTabs, AudioEditorTabNames, AudioEditorWindowIndex, AudioEditorTabTooltips, true, nullptr, nullptr, false, false, nullptr, nullptr))
    {
        UIPageChanged();
    }
    ImGui::SetCursorScreenPos(clip_window_pos + ImVec2(labelWidth, 0));
    if (ImGui::BeginChild("##audio_editor_views", ImVec2(clip_window_size.x - labelWidth, clip_window_size.y), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 editor_view_window_pos = ImGui::GetCursorScreenPos();
        ImVec2 editor_view_window_size = ImGui::GetWindowSize();
        draw_list->AddRectFilled(editor_view_window_pos, editor_view_window_pos + editor_view_window_size, COL_DARK_ONE);
        switch (AudioEditorWindowIndex)
        {
            case 0: ShowAudioFilterWindow(draw_list); break;
            case 1: ShowAudioFusionWindow(draw_list); break;
            default: break;
        }
    }
    ImGui::EndChild();
}


/****************************************************************************************
 * 
 * Media Analyse windows
 *
 ***************************************************************************************/

static void ShowMediaAnalyseWindow(TimeLine *timeline, bool *expanded)
{
    ImGuiIO &io = ImGui::GetIO();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImVec2 window_pos = ImGui::GetCursorScreenPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    if (expanded && !*expanded)
    {
        // minimum view
        draw_list->AddRectFilled(window_pos, window_pos + ImVec2(window_size.x, 24), COL_DARK_ONE, 0);
    }
    else
    {
        ImVec2 scope_view_size = ImVec2(256, 256);
        ImRect scrop_rect = ImRect(window_pos, window_pos + scope_view_size);
        draw_list->AddRect(window_pos, window_pos + scope_view_size, COL_DARK_PANEL);
        draw_list->AddRectFilled(window_pos, window_pos + scope_view_size, IM_COL32_BLACK, 0);
        // control bar
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5, 0.5, 0.5, 0.5));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2, 0.2, 0.2, 1.0));

        ImGui::SetCursorScreenPos(window_pos + ImVec2(window_size.x - 32, 0));
        if (ImGui::Button(ICON_MORE "##select_scope"))
        {
            ImGui::OpenPopup("##select_scope_view");
        }
        if (ImGui::BeginPopup("##select_scope_view"))
        {
            for (int i = 0; i < IM_ARRAYSIZE(ScopeWindowTabNames); i++)
                if (ImGui::Selectable(ScopeWindowTabNames[i]))
                    ScopeWindowIndex = i;
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(window_pos + ImVec2(window_size.x - 32, 32));
        if (ImGui::Button(ICON_SETTING "##setting_scope"))
        {
            ImGui::OpenPopup("##setting_scope_view");
        }
        if (ImGui::BeginPopup("##setting_scope_view"))
        {
            switch (ScopeWindowIndex)
            {
                case 0:
                {
                    // histogram setting
                    ImGui::TextUnformatted("Log:"); ImGui::SameLine();
                    ImGui::ToggleButton("##histogram_logview", &g_media_editor_settings.HistogramLog);
                }
                break;
                case 1:
                {
                    // waveform setting
                    ImGui::TextUnformatted("Mirror:"); ImGui::SameLine();
                    ImGui::ToggleButton("##waveform_mirror", &g_media_editor_settings.WaveformMirror);
                    ImGui::TextUnformatted("Separate:"); ImGui::SameLine();
                    ImGui::ToggleButton("##waveform_separate", &g_media_editor_settings.WaveformSeparate);
                }
                break;
                case 2:
                {
                    // cie setting
                    bool cie_setting_changed = false;
                    ImGui::TextUnformatted("Show Color:"); ImGui::SameLine();
                    ImGui::ToggleButton("##cie_show_color", &g_media_editor_settings.CIEShowColor);
                    if (ImGui::Combo("Color System", (int *)&g_media_editor_settings.CIEColorSystem, color_system_items, IM_ARRAYSIZE(color_system_items)))
                    {
                        cie_setting_changed = true;
                    }
                    if (ImGui::Combo("Cie System", (int *)&g_media_editor_settings.CIEMode, cie_system_items, IM_ARRAYSIZE(cie_system_items)))
                    {
                        cie_setting_changed = true;
                    }
                    if (ImGui::Combo("Show Gamut", (int *)&g_media_editor_settings.CIEGamuts, color_system_items, IM_ARRAYSIZE(color_system_items)))
                    {
                        cie_setting_changed = true;
                    }
                    if (ImGui::DragFloat("Contrast##cie_contrast", &g_media_editor_settings.CIEContrast, 0.01f, 0.f, 1.f, "%.2f"))
                    {
                        cie_setting_changed = true;
                    }
                    ImGui::TextUnformatted("CorrectGamma:"); ImGui::SameLine();
                    if (ImGui::ToggleButton("##cie_correct_gamma", &g_media_editor_settings.CIECorrectGamma))
                    {
                        cie_setting_changed = true;
                    }
#if IMGUI_VULKAN_SHADER
                    if (cie_setting_changed && m_cie)
                    {
                        m_cie->SetParam(g_media_editor_settings.CIEColorSystem, 
                                        g_media_editor_settings.CIEMode, 512, 
                                        g_media_editor_settings.CIEGamuts, 
                                        g_media_editor_settings.CIEContrast, 
                                        g_media_editor_settings.CIECorrectGamma);
                    }
#endif
                }
                break;
                case 3:
                {
                    // vector setting
                }
                break;
                case 4:
                {
                    // audio dB setting
                }
                break;
                case 5:
                {
                    // audio spectrogram setting
                }
                break;
                default: break;
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(3);
        switch (ScopeWindowIndex)
        {
            case 0:
            {
                // histogram view
                ImGui::BeginGroup();
                ImGui::InvisibleButton("##histogram_view", scope_view_size);
                if (ImGui::IsItemHovered())
                {
                    if (io.MouseWheel < -FLT_EPSILON)
                    {
                        g_media_editor_settings.HistogramScale *= 0.9f;
                        if (g_media_editor_settings.HistogramScale < 0.01)
                            g_media_editor_settings.HistogramScale = 0.01;
                    }
                    else if (io.MouseWheel > FLT_EPSILON)
                    {
                        g_media_editor_settings.HistogramScale *= 1.1f;
                        if (g_media_editor_settings.HistogramScale > 4.0f)
                            g_media_editor_settings.HistogramScale = 4.0;
                    }
                }
                if (!mat_histogram.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
                    ImGui::SetCursorScreenPos(window_pos);
                    auto rmat = mat_histogram.channel(0);
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.f, 0.f, 0.f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.f, 0.f, 0.f, 0.5f));
                    ImGui::PlotLines("##rh", (float *)rmat.data, mat_histogram.w, 0, nullptr, 0, g_media_editor_settings.HistogramLog ? 10 : 1000, ImVec2(scope_view_size.x, scope_view_size.y / 3), 4, false, true);
                    ImGui::PopStyleColor(2);
                    ImGui::SetCursorScreenPos(window_pos + ImVec2(0, scope_view_size.y / 3));
                    auto gmat = mat_histogram.channel(1);
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.f, 1.f, 0.f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.f, 1.f, 0.f, 0.5f));
                    ImGui::PlotLines("##gh", (float *)gmat.data, mat_histogram.w, 0, nullptr, 0, g_media_editor_settings.HistogramLog ? 10 : 1000, ImVec2(scope_view_size.x, scope_view_size.y / 3), 4, false, true);
                    ImGui::PopStyleColor(2);
                    ImGui::SetCursorScreenPos(window_pos + ImVec2(0, scope_view_size.y * 2 / 3));
                    auto bmat = mat_histogram.channel(2);
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.f, 0.f, 1.f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.f, 0.f, 1.f, 0.5f));
                    ImGui::PlotLines("##bh", (float *)bmat.data, mat_histogram.w, 0, nullptr, 0, g_media_editor_settings.HistogramLog ? 10 : 1000, ImVec2(scope_view_size.x, scope_view_size.y / 3), 4, false, true);
                    ImGui::PopStyleColor(2);
                    ImGui::PopStyleColor();
                }
                draw_list->AddRect(scrop_rect.Min, scrop_rect.Max, COL_SLIDER_HANDLE, 0);
                // draw graticule line
                draw_list->PushClipRect(scrop_rect.Min, scrop_rect.Max);
                auto histogram_step = scope_view_size.x / 10;
                auto histogram_sub_vstep = scope_view_size.x / 50;
                auto histogram_vstep = scope_view_size.y / 3 * g_media_editor_settings.HistogramScale;
                auto histogram_seg = scope_view_size.y / 3 / histogram_vstep;
                for (int i = 1; i <= 10; i++)
                {
                    ImVec2 p0 = scrop_rect.Min + ImVec2(i * histogram_step, 0);
                    ImVec2 p1 = scrop_rect.Min + ImVec2(i * histogram_step, scrop_rect.Max.y);
                    draw_list->AddLine(p0, p1, COL_GRATICULE_DARK, 1);
                }
                for (int i = 0; i < histogram_seg; i++)
                {
                    ImVec2 pr0 = scrop_rect.Min + ImVec2(0, (scope_view_size.y / 3) - i * histogram_vstep);
                    ImVec2 pr1 = scrop_rect.Min + ImVec2(scrop_rect.Max.x, (scope_view_size.y / 3) - i * histogram_vstep);
                    draw_list->AddLine(pr0, pr1, IM_COL32(255, 128, 0, 32), 1);
                    ImVec2 pg0 = scrop_rect.Min + ImVec2(0, scope_view_size.y / 3) + ImVec2(0, (scope_view_size.y / 3) - i * histogram_vstep);
                    ImVec2 pg1 = scrop_rect.Min + ImVec2(0, scope_view_size.y / 3) + ImVec2(scrop_rect.Max.x, (scope_view_size.y / 3) - i * histogram_vstep);
                    draw_list->AddLine(pg0, pg1, IM_COL32(128, 255, 0, 32), 1);
                    ImVec2 pb0 = scrop_rect.Min + ImVec2(0, scope_view_size.y * 2 / 3) + ImVec2(0, (scope_view_size.y / 3) - i * histogram_vstep);
                    ImVec2 pb1 = scrop_rect.Min + ImVec2(0, scope_view_size.y * 2 / 3) + ImVec2(scrop_rect.Max.x, (scope_view_size.y / 3) - i * histogram_vstep);
                    draw_list->AddLine(pb0, pb1, IM_COL32(128, 128, 255, 32), 1);
                }
                for (int i = 0; i < 50; i++)
                {
                    ImVec2 p0 = scrop_rect.Min + ImVec2(i * histogram_sub_vstep, 0);
                    ImVec2 p1 = scrop_rect.Min + ImVec2(i * histogram_sub_vstep, 5);
                    draw_list->AddLine(p0, p1, COL_GRATICULE, 1);
                }
                draw_list->PopClipRect();
                ImGui::EndGroup();
            }
            break;
            case 1:
            {
                // waveform view
                ImGui::BeginGroup();
                ImGui::InvisibleButton("##waveform_view", scope_view_size);
                if (ImGui::IsItemHovered())
                {
                    if (io.MouseWheel < -FLT_EPSILON)
                    {
                        g_media_editor_settings.WaveformIntensity *= 0.9f;
                        if (g_media_editor_settings.WaveformIntensity < 0.1)
                            g_media_editor_settings.WaveformIntensity = 0.1;
                    }
                    else if (io.MouseWheel > FLT_EPSILON)
                    {
                        g_media_editor_settings.WaveformIntensity *= 1.1f;
                        if (g_media_editor_settings.WaveformIntensity > 4.0f)
                            g_media_editor_settings.WaveformIntensity = 4.0;
                    }
                }
                if (!mat_waveform.empty())
                {
                    ImGui::ImMatToTexture(mat_waveform, waveform_texture);
                    draw_list->AddImage(waveform_texture, scrop_rect.Min, scrop_rect.Max, g_media_editor_settings.WaveformMirror ? ImVec2(0, 1) : ImVec2(0, 0), g_media_editor_settings.WaveformMirror ? ImVec2(1, 0) : ImVec2(1, 1));
                }
                draw_list->AddRect(scrop_rect.Min, scrop_rect.Max, COL_SLIDER_HANDLE, 0);
                // draw graticule line
                draw_list->PushClipRect(scrop_rect.Min, scrop_rect.Max);
                auto waveform_step = scope_view_size.y / 10;
                auto waveform_vstep = scope_view_size.x / 10;
                auto waveform_sub_step = scope_view_size.y / 50;
                auto waveform_sub_vstep = scope_view_size.x / 100;
                for (int i = 0; i < 10; i++)
                {
                    ImVec2 p0 = scrop_rect.Min + ImVec2(0, i * waveform_step);
                    ImVec2 p1 = scrop_rect.Min + ImVec2(scrop_rect.Max.x, i * waveform_step);
                    if (i != 5)
                        draw_list->AddLine(p0, p1, COL_GRATICULE_DARK, 1);
                    else
                    {
                        ImGui::ImDrawListAddLineDashed(draw_list, p0, p1, COL_GRATICULE_DARK, 1, 100);
                    }
                    ImVec2 vp0 = scrop_rect.Min + ImVec2(i * waveform_vstep, 0);
                    ImVec2 vp1 = scrop_rect.Min + ImVec2(i * waveform_vstep, 10);
                    draw_list->AddLine(vp0, vp1, COL_GRATICULE, 1);
                }
                for (int i = 0; i < 50; i++)
                {
                    float l = i == 0 || i % 10 == 0 ? 10 : 5;
                    ImVec2 p0 = scrop_rect.Min + ImVec2(0, i * waveform_sub_step);
                    ImVec2 p1 = scrop_rect.Min + ImVec2(l, i * waveform_sub_step);
                    draw_list->AddLine(p0, p1, COL_GRATICULE, 1);
                }
                for (int i = 0; i < 100; i++)
                {
                    ImVec2 p0 = scrop_rect.Min + ImVec2(i * waveform_sub_vstep, 0);
                    ImVec2 p1 = scrop_rect.Min + ImVec2(i * waveform_sub_vstep, 5);
                    draw_list->AddLine(p0, p1, COL_GRATICULE, 1);
                }
                draw_list->PopClipRect();
                ImGui::EndGroup();
            }
            break;
            case 2:
            {
                // cie view
                ImGui::BeginGroup();
                ImGui::InvisibleButton("##cie_view", scope_view_size);
                if (ImGui::IsItemHovered())
                {
                    if (io.MouseWheel < -FLT_EPSILON)
                    {
                        g_media_editor_settings.CIEIntensity *= 0.9f;
                        if (g_media_editor_settings.CIEIntensity < 0.01)
                            g_media_editor_settings.CIEIntensity = 0.01;
                    }
                    else if (io.MouseWheel > FLT_EPSILON)
                    {
                        g_media_editor_settings.CIEIntensity *= 1.1f;
                        if (g_media_editor_settings.CIEIntensity > 1.0f)
                            g_media_editor_settings.CIEIntensity = 1.0;
                    }
                }
                if (!mat_cie.empty())
                {
                    ImGui::ImMatToTexture(mat_cie, cie_texture);
                    draw_list->AddImage(cie_texture, scrop_rect.Min, scrop_rect.Max, ImVec2(0, 0), ImVec2(1, 1));
                }
                draw_list->AddRect(scrop_rect.Min, scrop_rect.Max, COL_SLIDER_HANDLE, 0);
                // draw graticule line
                draw_list->PushClipRect(scrop_rect.Min, scrop_rect.Max);
                auto cie_step = scope_view_size.y / 10;
                auto cie_vstep = scope_view_size.x / 10;
                auto cie_sub_step = scope_view_size.y / 50;
                auto cie_sub_vstep = scope_view_size.x / 50;
                for (int i = 1; i <= 10; i++)
                {
                    ImVec2 hp0 = scrop_rect.Min + ImVec2(0, i * cie_step);
                    ImVec2 hp1 = scrop_rect.Min + ImVec2(scrop_rect.Max.x, i * cie_step);
                    draw_list->AddLine(hp0, hp1, COL_GRATICULE_DARK, 1);
                    ImVec2 vp0 = scrop_rect.Min + ImVec2(i * cie_vstep, 0);
                    ImVec2 vp1 = scrop_rect.Min + ImVec2(i * cie_vstep, scrop_rect.Max.y);
                    draw_list->AddLine(vp0, vp1, COL_GRATICULE_DARK, 1);
                }
                for (int i = 0; i < 50; i++)
                {
                    ImVec2 hp0 = scrop_rect.Min + ImVec2(scope_view_size.x - 3, i * cie_sub_step);
                    ImVec2 hp1 = scrop_rect.Min + ImVec2(scope_view_size.x, i * cie_sub_step);
                    draw_list->AddLine(hp0, hp1, COL_GRATICULE_HALF, 1);
                    ImVec2 vp0 = scrop_rect.Min + ImVec2(i * cie_sub_vstep, 0);
                    ImVec2 vp1 = scrop_rect.Min + ImVec2(i * cie_sub_vstep, 3);
                    draw_list->AddLine(vp0, vp1, COL_GRATICULE_HALF, 1);
                }
                std::string X_str = "X";
                std::string Y_str = "Y";
                if (g_media_editor_settings.CIEMode == ImGui::UCS)
                {
                    X_str = "U"; Y_str = "C";
                }
                else if (g_media_editor_settings.CIEMode == ImGui::LUV)
                {
                    X_str = "U"; Y_str = "V";
                }
                draw_list->AddText(scrop_rect.Min + ImVec2(2, 2), COL_GRATICULE, X_str.c_str());
                draw_list->AddText(scrop_rect.Min + ImVec2(scope_view_size.x - 12, scope_view_size.y - 18), COL_GRATICULE, Y_str.c_str());
                ImGui::SetWindowFontScale(0.7);
                for (int i = 0; i < 10; i++)
                {
                    if (i == 0) continue;
                    char mark[32] = {0};
                    ImFormatString(mark, IM_ARRAYSIZE(mark), "%.1f", i / 10.f);
                    draw_list->AddText(scrop_rect.Min + ImVec2(i * cie_vstep - 8, 2), COL_GRATICULE, mark);
                    draw_list->AddText(scrop_rect.Min + ImVec2(scope_view_size.x - 18, scope_view_size.y - i * cie_step - 6), COL_GRATICULE, mark);
                }
                ImGui::SetWindowFontScale(1.0);
                ImGui::PushStyleVar(ImGuiStyleVar_TexGlyphShadowOffset, ImVec2(1, 1));
                if (m_cie)
                {
                    ImVec2 white_point;
                    m_cie->GetWhitePoint((ImGui::ColorsSystems)g_media_editor_settings.CIEColorSystem, scope_view_size.x, scope_view_size.y, &white_point.x, &white_point.y);
                    draw_list->AddCircle(scrop_rect.Min + white_point, 3, IM_COL32_WHITE, 0, 2);
                    draw_list->AddCircle(scrop_rect.Min + white_point, 2, IM_COL32_BLACK, 0, 1);
                    ImVec2 green_point_system;
                    m_cie->GetGreenPoint((ImGui::ColorsSystems)g_media_editor_settings.CIEColorSystem, scope_view_size.x, scope_view_size.y, &green_point_system.x, &green_point_system.y);
                    draw_list->AddText(scrop_rect.Min + green_point_system, COL_GRATICULE, color_system_items[g_media_editor_settings.CIEColorSystem]);
                    ImVec2 green_point_gamuts;
                    m_cie->GetGreenPoint((ImGui::ColorsSystems)g_media_editor_settings.CIEGamuts, scope_view_size.x, scope_view_size.y, &green_point_gamuts.x, &green_point_gamuts.y);
                    draw_list->AddText(scrop_rect.Min + green_point_gamuts, COL_GRATICULE, color_system_items[g_media_editor_settings.CIEGamuts]);
                }
                ImGui::PopStyleVar();
                draw_list->PopClipRect();
                ImGui::EndGroup();
            }
            break;
            case 3:
            {
                // vector view
                ImGui::BeginGroup();
                ImGui::InvisibleButton("##vector_view", scope_view_size);
                if (ImGui::IsItemHovered())
                {
                    if (io.MouseWheel < -FLT_EPSILON)
                    {
                        g_media_editor_settings.VectorIntensity *= 0.9f;
                        if (g_media_editor_settings.VectorIntensity < 0.01)
                            g_media_editor_settings.VectorIntensity = 0.01;
                    }
                    else if (io.MouseWheel > FLT_EPSILON)
                    {
                        g_media_editor_settings.VectorIntensity *= 1.1f;
                        if (g_media_editor_settings.VectorIntensity > 1.0f)
                            g_media_editor_settings.VectorIntensity = 1.0;
                    }
                }
                if (!mat_vector.empty())
                {
                    ImGui::ImMatToTexture(mat_vector, vector_texture);
                    draw_list->AddImage(vector_texture, scrop_rect.Min, scrop_rect.Max, ImVec2(0, 0), ImVec2(1, 1));
                }
                draw_list->AddRect(scrop_rect.Min, scrop_rect.Max, COL_SLIDER_HANDLE, 0);
                // draw graticule line
                ImVec2 center_point = ImVec2(scrop_rect.Min + scope_view_size / 2);
                float radius = scope_view_size.x / 2;
                draw_list->PushClipRect(scrop_rect.Min, scrop_rect.Max);
                draw_list->AddCircle(center_point, radius, COL_GRATICULE_DARK, 0, 1);
                draw_list->AddLine(scrop_rect.Min + ImVec2(0, scope_view_size.y / 2), scrop_rect.Max - ImVec2(0, scope_view_size.y / 2), COL_GRATICULE_DARK);
                draw_list->AddLine(scrop_rect.Min + ImVec2(scope_view_size.x / 2, 0), scrop_rect.Max - ImVec2(scope_view_size.x / 2, 0), COL_GRATICULE_DARK);

                auto AngleToCoordinate = [&](float angle, float length)
                {
                    ImVec2 point(0, 0);
                    float hAngle = angle * M_PI / 180.f;
                    if (angle == 0.f)
                        point = ImVec2(length, 0);  // positive x axis
                    else if (angle == 180.f)
                        point = ImVec2(-length, 0); // negative x axis
                    else if (angle == 90.f)
                        point = ImVec2(0, length); // positive y axis
                    else if (angle == 270.f)
                        point = ImVec2(0, -length);  // negative y axis
                    else
                        point = ImVec2(length * cos(hAngle), length * sin(hAngle));
                    return point;
                };
                auto AngleToPoint = [&](float angle, float length)
                {
                    ImVec2 point = AngleToCoordinate(angle, length);
                    point = ImVec2(point.x * radius, -point.y * radius);
                    return point;
                };
                auto ColorToPoint = [&](float r, float g, float b)
                {
                    float angle, length, v;
                    ImGui::ColorConvertRGBtoHSV(r, g, b, angle, length, v);
                    angle = angle * 360;
                    auto point = AngleToCoordinate(angle, v);
                    point = ImVec2(point.x * radius, -point.y * radius);
                    return point;
                };

                for (int i = 0; i < 360; i+= 5)
                {
                    float l = 0.95;
                    if (i == 0 || i % 10 == 0)
                        l = 0.9;
                    auto p0 = AngleToPoint(i, 1.0);
                    auto p1 = AngleToPoint(i, l);
                    draw_list->AddLine(center_point + p0, center_point + p1, COL_GRATICULE_DARK);
                }

                auto draw_mark = [&](ImVec2 point, const char * mark)
                {
                    float rect_size = 12;
                    auto p0 = center_point + point - ImVec2(rect_size, rect_size);
                    auto p1 = center_point + point + ImVec2(rect_size, rect_size);
                    auto p2 = p0 + ImVec2(rect_size * 2, 0);
                    auto p3 = p0 + ImVec2(0, rect_size * 2);
                    auto text_size = ImGui::CalcTextSize(mark);
                    draw_list->AddText(p0 + ImVec2(rect_size - text_size.x / 2, rect_size - text_size.y / 2), COL_GRATICULE, mark);
                    draw_list->AddLine(p0, p0 + ImVec2(5, 0),   COL_GRATICULE, 2);
                    draw_list->AddLine(p0, p0 + ImVec2(0, 5),   COL_GRATICULE, 2);
                    draw_list->AddLine(p1, p1 + ImVec2(-5, 0),  COL_GRATICULE, 2);
                    draw_list->AddLine(p1, p1 + ImVec2(0, -5),  COL_GRATICULE, 2);
                    draw_list->AddLine(p2, p2 + ImVec2(-5, 0),  COL_GRATICULE, 2);
                    draw_list->AddLine(p2, p2 + ImVec2(0, 5),   COL_GRATICULE, 2);
                    draw_list->AddLine(p3, p3 + ImVec2(5, 0),   COL_GRATICULE, 2);
                    draw_list->AddLine(p3, p3 + ImVec2(0, -5),  COL_GRATICULE, 2);
                };

                draw_mark(ColorToPoint(0.75, 0, 0), "R");
                draw_mark(ColorToPoint(0, 0.75, 0), "G");
                draw_mark(ColorToPoint(0, 0, 0.75), "B");
                draw_mark(ColorToPoint(0.75, 0.75, 0), "Y");
                draw_mark(ColorToPoint(0.75, 0, 0.75), "M");
                draw_mark(ColorToPoint(0, 0.75, 0.75), "C");

                draw_list->PopClipRect();
                ImGui::EndGroup();
            }
            break;
            case 4:
            break;
            case 5:
            break;
            default: break;
        }
    }
}

#if 0
/****************************************************************************************
 * 
 * Media AI windows
 *
 ***************************************************************************************/
static void ShowMediaAIWindow(ImDrawList *draw_list)
{
    ImGui::SetWindowFontScale(1.2);
    ImGui::Indent(20);
    ImGui::PushStyleVar(ImGuiStyleVar_TexGlyphOutlineWidth, 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4, 0.4, 0.8, 0.8));
    ImGui::TextUnformatted("Meida AI");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::SetWindowFontScale(1.0);
}
#endif

/****************************************************************************************
 * 
 * Application Framework
 *
 ***************************************************************************************/
void Application_GetWindowProperties(ApplicationWindowProperty& property)
{
    property.name = "Media Editor";
    //property.viewport = false;
    property.docking = false;
    property.auto_merge = false;
    //property.power_save = false;
    property.width = DEFAULT_MAIN_VIEW_WIDTH;
    property.height = DEFAULT_MAIN_VIEW_HEIGHT;
}

void Application_SetupContext(ImGuiContext* ctx)
{
    if (!ctx)
        return;
    // Setup MediaEditorSetting
    ImGuiSettingsHandler setting_ini_handler;
    setting_ini_handler.TypeName = "MediaEditorSetting";
    setting_ini_handler.TypeHash = ImHashStr("MediaEditorSetting");
    setting_ini_handler.ReadOpenFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name) -> void*
    {
        return &g_media_editor_settings;
    };
    setting_ini_handler.ReadLineFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, void* entry, const char* line) -> void
    {
        MediaEditorSettings * setting = (MediaEditorSettings*)entry;
        int val_int = 0;
        int64_t val_int64 = 0;
        float val_float = 0;
        char val_path[1024] = {0};
        if (sscanf(line, "ProjectPath=%s", val_path) == 1) { setting->project_path = std::string(val_path); }
        else if (sscanf(line, "BottomViewExpanded=%d", &val_int) == 1) { setting->BottomViewExpanded = val_int == 1; }
        else if (sscanf(line, "TopViewHeight=%f", &val_float) == 1) { setting->TopViewHeight = val_float; }
        else if (sscanf(line, "BottomViewHeight=%f", &val_float) == 1) { setting->BottomViewHeight = val_float; }
        else if (sscanf(line, "OldBottomViewHeight=%f", &val_float) == 1) { setting->OldBottomViewHeight = val_float; }
        else if (sscanf(line, "ControlPanelWidth=%f", &val_float) == 1) { setting->ControlPanelWidth = val_float; }
        else if (sscanf(line, "MainViewWidth=%f", &val_float) == 1) { setting->MainViewWidth = val_float; }
        else if (sscanf(line, "VideoWidth=%d", &val_int) == 1) { setting->VideoWidth = val_int; }
        else if (sscanf(line, "VideoHeight=%d", &val_int) == 1) { setting->VideoHeight = val_int; }
        else if (sscanf(line, "VideoFrameRateNum=%d", &val_int) == 1) { setting->VideoFrameRate.num = val_int; }
        else if (sscanf(line, "VideoFrameRateDen=%d", &val_int) == 1) { setting->VideoFrameRate.den = val_int; }
        else if (sscanf(line, "PixelAspectRatioNum=%d", &val_int) == 1) { setting->PixelAspectRatio.num = val_int; }
        else if (sscanf(line, "PixelAspectRatioDen=%d", &val_int) == 1) { setting->PixelAspectRatio.den = val_int; }
        else if (sscanf(line, "VideoFrameCache=%d", &val_int) == 1) { setting->VideoFrameCacheSize = val_int; }
        else if (sscanf(line, "AudioChannels=%d", &val_int) == 1) { setting->AudioChannels = val_int; }
        else if (sscanf(line, "AudioSampleRate=%d", &val_int) == 1) { setting->AudioSampleRate = val_int; }
        else if (sscanf(line, "AudioFormat=%d", &val_int) == 1) { setting->AudioFormat = val_int; }
        else if (sscanf(line, "BankViewStyle=%d", &val_int) == 1) { setting->BankViewStyle = val_int; }
        else if (sscanf(line, "ShowHelpTips=%d", &val_int) == 1) { setting->ShowHelpTooltips = val_int == 1; }
        else if (sscanf(line, "HistogramLogView=%d", &val_int) == 1) { setting->HistogramLog = val_int == 1; }
        else if (sscanf(line, "HistogramScale=%f", &val_float) == 1) { setting->HistogramScale = val_float; }
        else if (sscanf(line, "WaveformMirror=%d", &val_int) == 1) { setting->WaveformMirror = val_int == 1; }
        else if (sscanf(line, "WaveformSeparate=%d", &val_int) == 1) { setting->WaveformSeparate = val_int == 1; }
        else if (sscanf(line, "WaveformIntensity=%f", &val_float) == 1) { setting->WaveformIntensity = val_float; }
        else if (sscanf(line, "CIECorrectGamma=%d", &val_int) == 1) { setting->CIECorrectGamma = val_int == 1; }
        else if (sscanf(line, "CIEShowColor=%d", &val_int) == 1) { setting->CIEShowColor = val_int == 1; }
        else if (sscanf(line, "CIEContrast=%f", &val_float) == 1) { setting->CIEContrast = val_float; }
        else if (sscanf(line, "CIEIntensity=%f", &val_float) == 1) { setting->CIEIntensity = val_float; }
        else if (sscanf(line, "CIEColorSystem=%d", &val_int) == 1) { setting->CIEColorSystem = val_int; }
        else if (sscanf(line, "CIEMode=%d", &val_int) == 1) { setting->CIEMode = val_int; }
        else if (sscanf(line, "CIEGamuts=%d", &val_int) == 1) { setting->CIEGamuts = val_int; }
        else if (sscanf(line, "VectorIntensity=%f", &val_float) == 1) { setting->VectorIntensity = val_float; }
        g_new_setting = g_media_editor_settings;
    };
    setting_ini_handler.WriteAllFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out_buf)
    {
        ImGuiContext& g = *ctx;
        out_buf->reserve(out_buf->size() + g.SettingsWindows.size() * 6); // ballpark reserve
        out_buf->appendf("[%s][##MediaEditorSetting]\n", handler->TypeName);
        out_buf->appendf("ProjectPath=%s\n", g_media_editor_settings.project_path.c_str());
        out_buf->appendf("BottomViewExpanded=%d\n", g_media_editor_settings.BottomViewExpanded ? 1 : 0);
        out_buf->appendf("TopViewHeight=%f\n", g_media_editor_settings.TopViewHeight);
        out_buf->appendf("BottomViewHeight=%f\n", g_media_editor_settings.BottomViewHeight);
        out_buf->appendf("OldBottomViewHeight=%f\n", g_media_editor_settings.OldBottomViewHeight);
        out_buf->appendf("ControlPanelWidth=%f\n", g_media_editor_settings.ControlPanelWidth);
        out_buf->appendf("MainViewWidth=%f\n", g_media_editor_settings.MainViewWidth);
        out_buf->appendf("VideoWidth=%d\n", g_media_editor_settings.VideoWidth);
        out_buf->appendf("VideoHeight=%d\n", g_media_editor_settings.VideoHeight);
        out_buf->appendf("VideoFrameRateNum=%d\n", g_media_editor_settings.VideoFrameRate.num);
        out_buf->appendf("VideoFrameRateDen=%d\n", g_media_editor_settings.VideoFrameRate.den);
        out_buf->appendf("PixelAspectRatioNum=%d\n", g_media_editor_settings.PixelAspectRatio.num);
        out_buf->appendf("PixelAspectRatioDen=%d\n", g_media_editor_settings.PixelAspectRatio.den);
        out_buf->appendf("VideoFrameCache=%d\n", g_media_editor_settings.VideoFrameCacheSize);
        out_buf->appendf("AudioChannels=%d\n", g_media_editor_settings.AudioChannels);
        out_buf->appendf("AudioSampleRate=%d\n", g_media_editor_settings.AudioSampleRate);
        out_buf->appendf("AudioFormat=%d\n", g_media_editor_settings.AudioFormat);
        out_buf->appendf("BankViewStyle=%d\n", g_media_editor_settings.BankViewStyle);
        out_buf->appendf("ShowHelpTips=%d\n", g_media_editor_settings.ShowHelpTooltips ? 1 : 0);
        out_buf->appendf("HistogramLogView=%d\n", g_media_editor_settings.HistogramLog ? 1 : 0);
        out_buf->appendf("HistogramScale=%f\n", g_media_editor_settings.HistogramScale);
        out_buf->appendf("WaveformMirror=%d\n", g_media_editor_settings.WaveformMirror ? 1 : 0);
        out_buf->appendf("WaveformSeparate=%d\n", g_media_editor_settings.WaveformSeparate ? 1 : 0);
        out_buf->appendf("WaveformIntensity=%f\n", g_media_editor_settings.WaveformIntensity);
        out_buf->appendf("CIECorrectGamma=%d\n", g_media_editor_settings.CIECorrectGamma ? 1 : 0);
        out_buf->appendf("CIEShowColor=%d\n", g_media_editor_settings.CIEShowColor ? 1 : 0);
        out_buf->appendf("CIEContrast=%f\n", g_media_editor_settings.CIEContrast);
        out_buf->appendf("CIEIntensity=%f\n", g_media_editor_settings.CIEIntensity);
        out_buf->appendf("CIEColorSystem=%d\n", g_media_editor_settings.CIEColorSystem);
        out_buf->appendf("CIEMode=%d\n", g_media_editor_settings.CIEMode);
        out_buf->appendf("CIEGamuts=%d\n", g_media_editor_settings.CIEGamuts);
        out_buf->appendf("VectorIntensity=%f\n", g_media_editor_settings.VectorIntensity);
        out_buf->append("\n");
    };
    setting_ini_handler.ApplyAllFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler)
    {
        // handle project after all setting is loaded 
        if (!g_media_editor_settings.project_path.empty())
        {
            if (LoadProject(g_media_editor_settings.project_path) == 0)
                quit_save_confirm = false;
        }
#if IMGUI_VULKAN_SHADER
        if (m_cie) 
            m_cie->SetParam(g_media_editor_settings.CIEColorSystem, 
                            g_media_editor_settings.CIEMode, 512, 
                            g_media_editor_settings.CIEGamuts, 
                            g_media_editor_settings.CIEContrast, 
                            g_media_editor_settings.CIECorrectGamma);
#endif
    };
    ctx->SettingsHandlers.push_back(setting_ini_handler);

#ifdef USE_BOOKMARK
    ImGuiSettingsHandler bookmark_ini_handler;
    bookmark_ini_handler.TypeName = "BookMark";
    bookmark_ini_handler.TypeHash = ImHashStr("BookMark");
    bookmark_ini_handler.ReadOpenFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name) -> void*
    {
        return ImGuiFileDialog::Instance();
    };
    bookmark_ini_handler.ReadLineFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, void* entry, const char* line) -> void
    {
        IGFD::FileDialog * dialog = (IGFD::FileDialog *)entry;
        dialog->DeserializeBookmarks(line);
    };
    bookmark_ini_handler.WriteAllFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out_buf)
    {
        ImGuiContext& g = *ctx;
        out_buf->reserve(out_buf->size() + g.SettingsWindows.size() * 6); // ballpark reserve
        auto bookmark = ImGuiFileDialog::Instance()->SerializeBookmarks();
        out_buf->appendf("[%s][##%s]\n", handler->TypeName, handler->TypeName);
        out_buf->appendf("%s\n", bookmark.c_str());
        out_buf->append("\n");
    };
    ctx->SettingsHandlers.push_back(bookmark_ini_handler);
#endif
}

void Application_Initialize(void** handle)
{
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImFontAtlas* atlas = io.Fonts;
    ImFont* font = atlas->Fonts[1];
    io.FontDefault = font;
    io.IniFilename = ini_file.c_str();
    if (io.ConfigFlags & ImGuiConfigFlags_EnableLowRefreshMode)
    {
        ImGui::SetTableLabelBreathingSpeed(0.01, 0.5);
        ui_breathing_step = 0.02;
    }
    else
    {
        ImGui::SetTableLabelBreathingSpeed(0.005, 0.5);
        ui_breathing_step = 0.01;
    }
    ImGui::ResetTabLabelStyle(ImGui::ImGuiTabLabelStyle_Dark, *tab_style);

    Logger::GetDefaultLogger()->SetShowLevels(Logger::DEBUG);
    // GetMediaReaderLogger()->SetShowLevels(Logger::DEBUG);
    // GetSnapshotGeneratorLogger()->SetShowLevels(Logger::DEBUG);
#if IMGUI_VULKAN_SHADER
    int gpu = ImGui::get_default_gpu_index();
    m_histogram = new ImGui::Histogram_vulkan(gpu);
    m_waveform = new ImGui::Waveform_vulkan(gpu);
    m_cie = new ImGui::CIE_vulkan(gpu);
    m_vector = new ImGui::Vector_vulkan(gpu);
#endif
    NewTimeline();
}

void Application_Finalize(void** handle)
{
    if (timeline) { delete timeline; timeline = nullptr; }
    if (m_histogram) { delete m_histogram; m_histogram = nullptr; }
    if (m_waveform) { delete m_waveform; m_waveform = nullptr; }
    if (m_cie) { delete m_cie; m_cie = nullptr; }
    if (m_vector) {delete m_vector; m_vector = nullptr; }
    if (waveform_texture) { ImGui::ImDestroyTexture(waveform_texture); waveform_texture = nullptr; }
    if (cie_texture) { ImGui::ImDestroyTexture(cie_texture); cie_texture = nullptr; }
    if (vector_texture) { ImGui::ImDestroyTexture(vector_texture); vector_texture = nullptr; }
}

bool Application_Frame(void * handle, bool app_will_quit)
{
    static bool app_done = false;
    const float media_icon_size = 96; 
    const float scope_height = 256;
    const float tool_icon_size = 32;
    static bool show_about = false;
    static bool show_configure = false;
    static bool show_debug = false;
    auto platform_io = ImGui::GetPlatformIO();
    
    const ImGuiFileDialogFlags fflags = ImGuiFileDialogFlags_ShowBookmark | ImGuiFileDialogFlags_DisableCreateDirectoryButton;
    const std::string video_file_dis = "*.mp4 *.mov *.mkv *.avi *.webm *.ts";
    const std::string video_file_surfix = ".mp4,.mov,.mkv,.avi,.webm,.ts";
    const std::string audio_file_dis = "*.wav *.mp3 *.aac *.ogg *.ac3 *.dts";
    const std::string audio_file_surfix = ".wav,.mp3,.aac,.ogg,.ac3,.dts";
    const std::string image_file_dis = "*.png *.gif *.jpg *.jpeg *.tiff *.webp";
    const std::string image_file_surfix = ".png,.gif,.jpg,.jpeg,.tiff,.webp";
    const std::string video_filter = "Video files (" + video_file_dis + "){" + video_file_surfix + "}";
    const std::string audio_filter = "Audio files (" + audio_file_dis + "){" + audio_file_surfix + "}";
    const std::string image_filter = "Image files (" + image_file_dis + "){" + image_file_surfix + "}";
    const std::string ffilters = "All Support Files (" + video_file_dis + " " + audio_file_dis + " " + image_file_dis + ")" + "{" +
                                                        video_file_surfix + "," + audio_file_surfix + "," + image_file_surfix + "}" + "," +
                                                        video_filter + "," +
                                                        audio_filter + "," +
                                                        image_filter + "," +
                                                        ".*";
    const ImGuiFileDialogFlags pflags = ImGuiFileDialogFlags_ShowBookmark | ImGuiFileDialogFlags_ConfirmOverwrite;
    const std::string pfilters = "Project files (*.mep){.mep},.*";
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    static const int numControlPanelTabs = sizeof(ControlPanelTabNames)/sizeof(ControlPanelTabNames[0]);
    static const int numMainWindowTabs = sizeof(MainWindowTabNames)/sizeof(MainWindowTabNames[0]);
    bool multiviewport = io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | 
                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;
    if (multiviewport)
    {
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    }
    else
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_None);
    }
    UpdateBreathing();
    ImGui::Begin("Content", nullptr, flags);
    // for debug
    if (show_debug) ImGui::ShowMetricsWindow(&show_debug);
    // for debug end
    if (show_about)
    {
        ImGui::OpenPopup(ICON_FA5_INFO_CIRCLE " About", ImGuiPopupFlags_AnyPopup);
    }
    if (ImGui::BeginPopupModal(ICON_FA5_INFO_CIRCLE " About", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        ShowAbout();
        int i = ImGui::GetCurrentWindow()->ContentSize.x;
        ImGui::Indent((i - 40.0f) * 0.5f);
        if (ImGui::Button("OK", ImVec2(40, 0))) { show_about = false; ImGui::CloseCurrentPopup(); }
        ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }
    if (show_configure)
    {
        ImGui::OpenPopup(ICON_FA_WHMCS " Configure", ImGuiPopupFlags_AnyPopup);
    }
    if (ImGui::BeginPopupModal(ICON_FA_WHMCS " Configure", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        ShowConfigure(g_new_setting);
        int i = ImGui::GetCurrentWindow()->ContentSize.x;
        ImGui::Indent((i - 140.0f) * 0.5f);
        if (ImGui::Button("OK", ImVec2(60, 0)))
        {
            show_configure = false;
            g_media_editor_settings = g_new_setting;
            if (timeline)
            {
                timeline->mWidth = g_media_editor_settings.VideoWidth;
                timeline->mHeight = g_media_editor_settings.VideoHeight;
                timeline->mFrameRate = g_media_editor_settings.VideoFrameRate;
                timeline->mMaxCachedVideoFrame = g_media_editor_settings.VideoFrameCacheSize > 0 ? g_media_editor_settings.VideoFrameCacheSize : 10;
                timeline->mAudioSampleRate = g_media_editor_settings.AudioSampleRate;
                timeline->mAudioChannels = g_media_editor_settings.AudioChannels;
                timeline->mAudioFormat = (AudioRender::PcmFormat)g_media_editor_settings.AudioFormat;
                timeline->mShowHelpTooltips = g_media_editor_settings.ShowHelpTooltips;
            }
            ImGui::CloseCurrentPopup(); 
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(60, 0)))
        {
            show_configure = false;
            g_new_setting = g_media_editor_settings;
            ImGui::CloseCurrentPopup(); 
        }
        ImGui::EndPopup();
    }
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImVec2 window_pos = ImGui::GetWindowPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    if (multiviewport)
    {
        window_pos = viewport->WorkPos;
        window_size = viewport->WorkSize;
    }

    ImGui::PushID("##Main_Timeline");
    float main_height = g_media_editor_settings.TopViewHeight * window_size.y;
    float timeline_height = g_media_editor_settings.BottomViewHeight * window_size.y;
    ImGui::Splitter(false, 4.0f, &main_height, &timeline_height, 32, 32);
    g_media_editor_settings.TopViewHeight = main_height / window_size.y;
    g_media_editor_settings.BottomViewHeight = timeline_height / window_size.y;
    ImGui::PopID();
    ImVec2 main_pos = window_pos + ImVec2(4, 0);
    ImVec2 main_size(window_size.x, main_height + 4);
    ImGui::SetNextWindowPos(main_pos, ImGuiCond_Always);
    if (ImGui::BeginChild("##Top_Panel", main_size, false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 main_window_size = ImGui::GetWindowSize();
        ImGui::PushID("##Control_Panel_Main");
        float control_pane_width = g_media_editor_settings.ControlPanelWidth * main_window_size.x;
        float main_width = g_media_editor_settings.MainViewWidth * main_window_size.x;
        ImGui::Splitter(true, 4.0f, &control_pane_width, &main_width, media_icon_size + tool_icon_size, 96);
        g_media_editor_settings.ControlPanelWidth = control_pane_width / main_window_size.x;
        g_media_editor_settings.MainViewWidth = main_width / main_window_size.x;
        ImGui::PopID();
        
        // add left tool bar
        ImGui::SetCursorPos(ImVec2(0, tool_icon_size));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5, 0.5, 0.5, 0.5));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2, 0.2, 0.2, 1.0));
        if (ImGui::Button(ICON_NEW_PROJECT "##NewProject", ImVec2(tool_icon_size, tool_icon_size)))
        {
            // New Project
            NewProject();
        }
        ImGui::ShowTooltipOnHover("New Project");
        if (ImGui::Button(ICON_OPEN_PROJECT "##OpenProject", ImVec2(tool_icon_size, tool_icon_size)))
        {
            // Open Project
            ImGuiFileDialog::Instance()->OpenModal("##MediaEditFileDlgKey", ICON_IGFD_FOLDER_OPEN " Open Project File", 
                                                    pfilters.c_str(),
                                                    ".",
                                                    1, 
                                                    IGFDUserDatas("ProjectOpen"), 
                                                    fflags);
        }
        ImGui::ShowTooltipOnHover("Open Project ...");
        if (ImGui::Button(ICON_SAVE_PROJECT "##SaveProject", ImVec2(tool_icon_size, tool_icon_size)))
        {
            // Save Project
            ImGuiFileDialog::Instance()->OpenModal("##MediaEditFileDlgKey", ICON_IGFD_FOLDER_OPEN " Save Project File", 
                                                    pfilters.c_str(),
                                                    ".",
                                                    1, 
                                                    IGFDUserDatas("ProjectSave"), 
                                                    pflags);
        }
        ImGui::ShowTooltipOnHover("Save Project As...");
        if (ImGui::Button(ICON_IGFD_ADD "##AddMedia", ImVec2(tool_icon_size, tool_icon_size)))
        {
            // Open Media Source
            ImGuiFileDialog::Instance()->OpenModal("##MediaEditFileDlgKey", ICON_IGFD_FOLDER_OPEN " Choose Media File", 
                                                    ffilters.c_str(),
                                                    ".",
                                                    1, 
                                                    IGFDUserDatas("Media Source"), 
                                                    fflags);
        }
        ImGui::ShowTooltipOnHover("Add new media into bank");
        if (ImGui::Button(ICON_FA_WHMCS "##Configure", ImVec2(tool_icon_size, tool_icon_size)))
        {
            // Show Setting
            show_configure = true;
        }
        ImGui::ShowTooltipOnHover("Configure");
        if (ImGui::Button(ICON_FA5_INFO_CIRCLE "##About", ImVec2(tool_icon_size, tool_icon_size)))
        {
            // Show About
            show_about = true;
        }
        ImGui::ShowTooltipOnHover("About Media Editor");
        if (ImGui::Button(ICON_UI_DEBUG "##UIDebug", ImVec2(tool_icon_size, tool_icon_size)))
        {
            // open debug window
            show_debug = !show_debug;
        }
        ImGui::ShowTooltipOnHover("UI Metric");
        ImGui::PopStyleColor(3);

        // add banks window
        ImVec2 bank_pos = window_pos + ImVec2(4 + tool_icon_size, 0);
        ImVec2 bank_size(control_pane_width - 4 - tool_icon_size, main_window_size.y - 4);
        ImGui::SetNextWindowPos(bank_pos, ImGuiCond_Always);
        if (ImGui::BeginChild("##Control_Panel_Window", bank_size, false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
        {
            ImVec2 bank_window_size = ImGui::GetWindowSize();
            ImGui::TabLabels(numControlPanelTabs, ControlPanelTabNames, ControlPanelIndex, ControlPanelTabTooltips , false, true, nullptr, nullptr, false, false, nullptr, nullptr);

            // make control panel area
            ImVec2 area_pos = window_pos + ImVec2(tool_icon_size + 4, 32);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_DARK_ONE);
            ImGui::SetNextWindowPos(area_pos, ImGuiCond_Always);
            if (ImGui::BeginChild("##Control_Panel_content", bank_window_size - ImVec2(4, 32), false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
            {
                ImDrawList *draw_list = ImGui::GetWindowDrawList();
                switch (ControlPanelIndex)
                {
                    case 0: ShowMediaBankWindow(draw_list, media_icon_size); break;
                    case 1: ShowTransitionBankWindow(draw_list); break;
                    case 2: 
                        switch (g_media_editor_settings.BankViewStyle)
                        {
                            case 0: ShowFilterBankIconWindow(draw_list); break;
                            case 1: ShowFilterBankTreeWindow(draw_list); break;
                            default: break;
                        }
                    break;
                    case 3: ShowMediaOutputWindow(draw_list); break;
                    default: break;
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        ImVec2 main_sub_pos = window_pos + ImVec2(control_pane_width + 8, 0);
        ImVec2 main_sub_size(main_width - 8, main_window_size.y - 4);
        ImGui::SetNextWindowPos(main_sub_pos, ImGuiCond_Always);
        if (ImGui::BeginChild("##Main_Window", main_sub_size, false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar))
        {
            // full background
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            //ImVec2 main_window_size = ImGui::GetWindowSize();
            if (ImGui::TabLabels(numMainWindowTabs, MainWindowTabNames, MainWindowIndex, MainWindowTabTooltips , false, true, nullptr, nullptr, false, false, nullptr, nullptr))
            {
                UIPageChanged();
            }
            auto wmin = main_sub_pos + ImVec2(0, 32);
            auto wmax = wmin + ImGui::GetContentRegionAvail() - ImVec2(8, 0);
            draw_list->AddRectFilled(wmin, wmax, IM_COL32_BLACK, 8.0, ImDrawFlags_RoundCornersAll);
            if (ImGui::BeginChild("##Main_Window_content", wmax - wmin, false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
            {
                switch (MainWindowIndex)
                {
                    case 0: ShowMediaPreviewWindow(draw_list); break;
                    case 1: ShowVideoEditorWindow(draw_list); break;
                    case 2: ShowAudioEditorWindow(draw_list); break;
                    default: break;
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    
    bool _expanded = g_media_editor_settings.BottomViewExpanded;
    ImVec2 panel_pos = window_pos + ImVec2(4, g_media_editor_settings.TopViewHeight * window_size.y + 12);
    ImVec2 panel_size(window_size.x - 4 - scope_height - 32, g_media_editor_settings.BottomViewHeight * window_size.y - 12);
    ImGui::SetNextWindowPos(panel_pos, ImGuiCond_Always);
    if (ImGui::BeginChild("##Timeline", panel_size, false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
    {
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        bool overExpanded = ExpendButton(draw_list, ImVec2(panel_pos.x + 2, panel_pos.y + 2), !_expanded);
        if (overExpanded && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            _expanded = !_expanded;
        ImGui::SetCursorScreenPos(panel_pos + ImVec2(32, 0));
        DrawTimeLine(timeline,  &_expanded);
        if (g_media_editor_settings.BottomViewExpanded != _expanded)
        {
            if (!_expanded)
            {
                g_media_editor_settings.OldBottomViewHeight = g_media_editor_settings.BottomViewHeight;
                g_media_editor_settings.BottomViewHeight = 60.0f / window_size.y;
                g_media_editor_settings.TopViewHeight = 1 - g_media_editor_settings.BottomViewHeight;
            }
            else
            {
                g_media_editor_settings.BottomViewHeight = g_media_editor_settings.OldBottomViewHeight;
                g_media_editor_settings.TopViewHeight = 1.0f - g_media_editor_settings.BottomViewHeight;
            }
            g_media_editor_settings.BottomViewExpanded = _expanded;
        }
    }
    ImGui::EndChild();

    ImVec2 scope_pos = panel_pos + ImVec2(panel_size.x, 0);
    ImVec2 scope_size = ImVec2(scope_height + 32, scope_height);
    ImGui::SetNextWindowPos(scope_pos, ImGuiCond_Always);
    if (ImGui::BeginChild("##Scope_View", scope_size, false, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings))
    {
        ShowMediaAnalyseWindow(timeline, &_expanded);
    }
    ImGui::EndChild();
    
    ImGui::PopStyleColor();
    ImGui::End();

    if (MainWindowIndex == 0)
    {
        // preview view
        if (MonitorIndexPreviewVideo != -1 && MonitorIndexPreviewVideo < platform_io.Monitors.Size)
        {
            std::string preview_window_lable = "Preview_view_windows" + std::to_string(MonitorIndexPreviewVideo);
            auto mon = platform_io.Monitors[MonitorIndexPreviewVideo];
            ImGui::SetNextWindowPos(mon.MainPos);
            ImGui::SetNextWindowSize(mon.MainSize);
            ImGui::Begin(preview_window_lable.c_str(), nullptr, flags | ImGuiWindowFlags_FullScreen);
            ShowVideoWindow(timeline->mMainPreviewTexture, mon.MainPos, mon.MainSize);
            ImGui::SetCursorScreenPos(mon.MainPos + ImVec2(80, 60));
            ImGui::TextComplex("Preview", 3.0f, ImVec4(0.8, 0.8, 0.8, 0.2),
                                0.1f, ImVec4(0.8, 0.8, 0.8, 0.3),
                                ImVec2(4, 4), ImVec4(0.0, 0.0, 0.0, 0.5));
            ImGui::End();
        }
    }
    else if (MainWindowIndex == 1 && VideoEditorWindowIndex == 0)
    {
        // video filter
        if (MonitorIndexVideoFilterOrg != -1 && MonitorIndexVideoFilterOrg < platform_io.Monitors.Size)
        {
            std::string view_window_lable = "video_filter_org_windows" + std::to_string(MonitorIndexVideoFilterOrg);
            auto mon = platform_io.Monitors[MonitorIndexVideoFilterOrg];
            ImGui::SetNextWindowPos(mon.MainPos);
            ImGui::SetNextWindowSize(mon.MainSize);
            ImGui::Begin(view_window_lable.c_str(), nullptr, flags | ImGuiWindowFlags_FullScreen);
            ShowVideoWindow(timeline->mVideoFilterInputTexture, mon.MainPos, mon.MainSize);
            ImGui::SetCursorScreenPos(mon.MainPos + ImVec2(80, 60));
            ImGui::TextComplex("Filter Input", 3.0f, ImVec4(0.8, 0.8, 0.8, 0.2),
                                0.1f, ImVec4(0.8, 0.8, 0.8, 0.3),
                                ImVec2(4, 4), ImVec4(0.0, 0.0, 0.0, 0.5));
            ImGui::End();
        }
        if (MonitorIndexVideoFiltered != -1 && MonitorIndexVideoFiltered < platform_io.Monitors.Size)
        {
            std::string view_window_lable = "video_filter_output_windows" + std::to_string(MonitorIndexVideoFiltered);
            auto mon = platform_io.Monitors[MonitorIndexVideoFiltered];
            ImGui::SetNextWindowPos(mon.MainPos);
            ImGui::SetNextWindowSize(mon.MainSize);
            ImGui::Begin(view_window_lable.c_str(), nullptr, flags | ImGuiWindowFlags_FullScreen);
            ShowVideoWindow(timeline->mVideoFilterOutputTexture, mon.MainPos, mon.MainSize);
            ImGui::SetCursorScreenPos(mon.MainPos + ImVec2(80, 60));
            ImGui::TextComplex("Filter Output", 3.0f, ImVec4(0.8, 0.8, 0.8, 0.2),
                                0.1f, ImVec4(0.8, 0.8, 0.8, 0.3),
                                ImVec2(4, 4), ImVec4(0.0, 0.0, 0.0, 0.5));
            ImGui::End();
        }
    }

    if (multiviewport)
    {
        ImGui::PopStyleVar(2);
    }
    // check save stage if app will quit
    if (app_will_quit && timeline)
    {
        if (timeline->m_Tracks.size() > 0 || timeline->media_items.size() > 0) // TODO::Dicky Check timeline changed later
        {
            if (quit_save_confirm || g_media_editor_settings.project_path.empty())
            {
                ImGuiFileDialog::Instance()->OpenModal("##MediaEditFileDlgKey", ICON_IGFD_FOLDER_OPEN " Save Project File", 
                                                    pfilters.c_str(),
                                                    ".",
                                                    1, 
                                                    IGFDUserDatas("ProjectSaveQuit"), 
                                                    pflags);
            }
            else
            {
                SaveProject(g_media_editor_settings.project_path);
                app_done = app_will_quit;
            }
        }
        else
        {
            quit_save_confirm = false;
        }
    }
    // File Dialog
    ImVec2 minSize = ImVec2(600, 600);
	ImVec2 maxSize = ImVec2(FLT_MAX, FLT_MAX);
    if (ImGuiFileDialog::Instance()->Display("##MediaEditFileDlgKey", ImGuiWindowFlags_NoCollapse, minSize, maxSize))
    {
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            auto file_path = ImGuiFileDialog::Instance()->GetFilePathName();
            auto file_name = ImGuiFileDialog::Instance()->GetCurrentFileName();
            auto file_surfix = ImGuiFileDialog::Instance()->GetCurrentFileSurfix();
            auto userDatas = std::string((const char*)ImGuiFileDialog::Instance()->GetUserDatas());
            if (userDatas.compare("Media Source") == 0)
            {
                MediaTimeline::MEDIA_TYPE type = MEDIA_UNKNOWN;
                if (!file_surfix.empty())
                {
                    if ((file_surfix.compare(".mp4") == 0) ||
                        (file_surfix.compare(".mov") == 0) ||
                        (file_surfix.compare(".mkv") == 0) ||
                        (file_surfix.compare(".avi") == 0) ||
                        (file_surfix.compare(".webm") == 0) ||
                        (file_surfix.compare(".ts") == 0))
                        type = MEDIA_VIDEO;
                    else 
                        if ((file_surfix.compare(".wav") == 0) ||
                            (file_surfix.compare(".mp3") == 0) ||
                            (file_surfix.compare(".aac") == 0) ||
                            (file_surfix.compare(".ac3") == 0) ||
                            (file_surfix.compare(".dts") == 0) ||
                            (file_surfix.compare(".ogg") == 0))
                        type = MEDIA_AUDIO;
                    else 
                        if ((file_surfix.compare(".jpg") == 0) ||
                            (file_surfix.compare(".jpeg") == 0) ||
                            (file_surfix.compare(".png") == 0) ||
                            (file_surfix.compare(".gif") == 0) ||
                            (file_surfix.compare(".tiff") == 0) ||
                            (file_surfix.compare(".webp") == 0))
                        type = MEDIA_PICTURE;
                }
                if (timeline)
                {
                    // check media is already in bank
                    auto iter = std::find_if(timeline->media_items.begin(), timeline->media_items.end(), [file_name, file_path, type](const MediaItem* item)
                    {
                        return  file_name.compare(item->mName) == 0 &&
                                file_path.compare(item->mPath) == 0 &&
                                type == item->mMediaType;
                    });
                    if (iter == timeline->media_items.end())
                    {
                        MediaItem * item = new MediaItem(file_name, file_path, type, timeline);
                        timeline->media_items.push_back(item);
                    }
                }
            }
            if (userDatas.compare("ProjectOpen") == 0)
            {
                if (!g_media_editor_settings.project_path.empty())
                {
                    SaveProject(g_media_editor_settings.project_path);
                }
                LoadProject(file_path);
            }
            if (userDatas.compare("ProjectSave") == 0)
            {
                SaveProject(file_path);
            }
            if (userDatas.compare("ProjectSaveQuit") == 0)
            {
                SaveProject(file_path);
                app_done = true;
            }
        }
        else
        {
            auto userDatas = std::string((const char*)ImGuiFileDialog::Instance()->GetUserDatas());
            if (userDatas.compare("ProjectSaveQuit") == 0)
                app_done = true;
        }
        ImGuiFileDialog::Instance()->Close();
    }
    else if (!quit_save_confirm)
    {
        app_done = app_will_quit;
    }

    return app_done;
}