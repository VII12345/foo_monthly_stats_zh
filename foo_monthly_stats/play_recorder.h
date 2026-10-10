#pragma once
#include "stdafx.h"
#include "playback_time_accumulator.h"

namespace fms
{
    // Tracks wall-clock listening time while playback is active. Playback counts
    // remain independently controlled by foobar2000's playback statistics callback.
    class PlaybackTimeTracker : public play_callback_static
    {
    public:
        unsigned get_flags() override
        {
            return flag_on_playback_starting | flag_on_playback_new_track |
                   flag_on_playback_stop | flag_on_playback_pause | flag_on_playback_seek;
        }

        void on_playback_starting(play_control::t_track_command command, bool paused) override;
        void on_playback_new_track(metadb_handle_ptr track) override;
        void on_playback_stop(play_control::t_stop_reason reason) override;
        void on_playback_pause(bool paused) override;

        void on_playback_seek(double) override;
        void on_playback_edited(metadb_handle_ptr) override {}
        void on_playback_dynamic_info(const file_info &) override {}
        void on_playback_dynamic_info_track(const file_info &) override {}
        void on_playback_time(double) override {}
        void on_volume_change(float) override {}

        static double get_played_time(metadb_handle_ptr track);

    private:
        static void beginActiveInterval();
        static void finishCurrentTrack(const char *source);

        static metadb_handle_ptr s_current_track;
        static PlaybackTimeAccumulator s_time_accumulator;
        static bool s_paused;
    };

    class PlaybackStatsCollector : public playback_statistics_collector
    {
    public:
        void on_item_played(metadb_handle_ptr p_item) override;
    };

} // namespace fms
