#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include "Logger.h"
#include "SubtitleClip.h"

namespace DataLayer
{
    struct SubtitleTrack;
    using SubtitleTrackHolder = std::shared_ptr<SubtitleTrack>;

    struct SubtitleTrack
    {
        virtual int64_t Id() const = 0;
        virtual uint32_t ClipCount() const = 0;
        virtual int64_t Duration() const = 0;
        virtual const SubtitleStyle& DefaultStyle() const = 0;

        virtual bool SetFrameSize(uint32_t width, uint32_t height) = 0;
        virtual bool EnableFullSizeOutput(bool enable) = 0;
        virtual bool SetFont(const std::string& font) = 0;
        virtual bool SetScale(double value) = 0;
        virtual bool SetScaleX(double value) = 0;
        virtual bool SetScaleY(double value) = 0;
        virtual bool SetSpacing(double value) = 0;
        virtual bool SetAngle(double value) = 0;
        virtual bool SetOutlineWidth(double value) = 0;
        virtual bool SetAlignment(int value) = 0;
        virtual bool SetMarginH(int value) = 0;
        virtual bool SetMarginV(int value) = 0;
        virtual bool SetItalic(int value) = 0;
        virtual bool SetBold(int value) = 0;
        virtual bool SetUnderLine(bool enable) = 0;
        virtual bool SetStrikeOut(bool enable) = 0;
        virtual bool SetPrimaryColor(const SubtitleColor& color) = 0;
        virtual bool SetSecondaryColor(const SubtitleColor& color) = 0;
        virtual bool SetOutlineColor(const SubtitleColor& color) = 0;
        virtual bool SetBackgroundColor(const SubtitleColor& color) = 0;
        virtual bool SetPrimaryColor(const ImVec4& color) = 0;
        virtual bool SetSecondaryColor(const ImVec4& color) = 0;
        virtual bool SetOutlineColor(const ImVec4& color) = 0;
        virtual bool ChangeClipTime(SubtitleClipHolder clip, int64_t startTime, int64_t duration) = 0;

        virtual SubtitleClipHolder NewClip(int64_t startTime, int64_t duration) = 0;
        virtual SubtitleClipHolder GetClipByTime(int64_t ms) = 0;
        virtual SubtitleClipHolder GetCurrClip() = 0;
        virtual SubtitleClipHolder GetPrevClip() = 0;
        virtual SubtitleClipHolder GetNextClip() = 0;
        virtual int32_t GetClipIndex(SubtitleClipHolder clip) const = 0;
        virtual uint32_t GetCurrIndex() const = 0;
        virtual bool SeekToTime(int64_t ms) = 0;
        virtual bool SeekToIndex(uint32_t index) = 0;

        virtual bool ChangeText(uint32_t clipIndex, const std::string& text) = 0;
        virtual bool ChangeText(SubtitleClipHolder clip, const std::string& text) = 0;
        virtual bool SaveAs(const std::string& subFilePath) = 0;

        virtual std::string GetError() const = 0;

        static SubtitleTrackHolder BuildFromFile(int64_t id, const std::string& url);
        static SubtitleTrackHolder NewEmptyTrack(int64_t id);
    };

    bool InitializeSubtitleLibrary();
    void ReleaseSubtitleLibrary();
    bool SetFontDir(const std::string& path);
}

Logger::ALogger* GetSubtitleTrackLogger();