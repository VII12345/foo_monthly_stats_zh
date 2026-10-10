#include "stdafx.h"
#include "play_recorder.h"
#include "db_manager.h"
#include "preferences.h"
#include "track_identity.h"

namespace fms
{
    metadb_handle_ptr PlaybackTimeTracker::s_current_track;
    PlaybackTimeAccumulator PlaybackTimeTracker::s_time_accumulator;
    bool PlaybackTimeTracker::s_paused = false;

    static int64_t currentUnixTimeMs()
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER value;
        value.LowPart = ft.dwLowDateTime;
        value.HighPart = ft.dwHighDateTime;
        return static_cast<int64_t>((value.QuadPart / 10000ULL) - 11644473600000ULL);
    }

    static void record_listened_time(metadb_handle_ptr track, double seconds, const char *source)
    {
        if (!track.is_valid() || seconds < 1.0)
            return;

        file_info_impl fi;
        if (!track->get_info(fi))
            return;

        TrackInfo event{};
        event.path = track->get_path();
        event.title = fi.meta_get("TITLE", 0) ? fi.meta_get("TITLE", 0) : "";
        event.artist = fi.meta_get("ARTIST", 0) ? fi.meta_get("ARTIST", 0) : "";
        event.album = fi.meta_get("ALBUM", 0) ? fi.meta_get("ALBUM", 0) : "";
        event.track_crc = makeTrackIdentity(event.title, event.artist, event.path);
        event.play_count = 0;
        // Persist whole seconds so the displayed per-track values add up exactly
        // to the displayed period total.
        event.listened_seconds = static_cast<double>(std::llround(seconds));
        event.played_at = currentUnixTimeMs();

        console::formatter() << "[foo_monthly_stats] " << source << ": recording "
                             << event.title.c_str() << " (" << seconds << "s listened)";
        DbManager::get().postPlay(event);
    }

    void PlaybackTimeTracker::beginActiveInterval()
    {
        if (s_current_track.is_valid() && !s_paused)
            s_time_accumulator.resume();
    }

    void PlaybackTimeTracker::finishCurrentTrack(const char *source)
    {
        const double seconds = s_time_accumulator.finish();
        if (s_current_track.is_valid())
            record_listened_time(s_current_track, seconds, source);

        s_current_track.release();
        s_time_accumulator.reset();
    }

    void PlaybackTimeTracker::on_playback_starting(play_control::t_track_command, bool paused)
    {
        // A new playback process is about to initialize; close any previous
        // interval now rather than charging decoder/startup time to the old track.
        finishCurrentTrack("on_playback_starting");
        s_paused = paused;
    }

    void PlaybackTimeTracker::on_playback_new_track(metadb_handle_ptr track)
    {
        finishCurrentTrack("on_playback_new_track");
        s_current_track = track;
        beginActiveInterval();
    }

    void PlaybackTimeTracker::on_playback_pause(bool paused)
    {
        if (paused)
            s_time_accumulator.pause();
        s_paused = paused;
        if (!paused)
            beginActiveInterval();
    }

    void PlaybackTimeTracker::on_playback_seek(double)
    {
        // Split the active interval at the seek callback. The skipped track
        // position is never added; timing resumes at the new playback position.
        s_time_accumulator.pause();
        beginActiveInterval();
    }

    void PlaybackTimeTracker::on_playback_stop(play_control::t_stop_reason reason)
    {
        if (reason == play_control::stop_reason_starting_another)
            return; // The new-track callback finalizes the outgoing track.

        finishCurrentTrack("on_playback_stop");
        s_paused = false;
    }

    double PlaybackTimeTracker::get_played_time(metadb_handle_ptr track)
    {
        if (!s_current_track.is_valid() || s_current_track != track)
            return 0.0;

        return s_time_accumulator.elapsed();
    }

    void PlaybackStatsCollector::on_item_played(metadb_handle_ptr item)
    {
        if (!item.is_valid())
            return;

        file_info_impl fi;
        if (!item->get_info(fi))
            return;

        TrackInfo event{};
        event.path = item->get_path();
        event.title = fi.meta_get("TITLE", 0) ? fi.meta_get("TITLE", 0) : "";
        event.artist = fi.meta_get("ARTIST", 0) ? fi.meta_get("ARTIST", 0) : "";
        event.album = fi.meta_get("ALBUM", 0) ? fi.meta_get("ALBUM", 0) : "";
        event.track_crc = makeTrackIdentity(event.title, event.artist, event.path);
        event.play_count = 1;
        event.listened_seconds = 0.0;
        event.played_at = currentUnixTimeMs();

        console::formatter() << "[foo_monthly_stats] on_item_played: "
                             << event.title.c_str() << " by " << event.artist.c_str();
        DbManager::get().postPlay(event);
    }

    static playback_statistics_collector_factory_t<PlaybackStatsCollector> g_playback_stats_collector;
    static play_callback_static_factory_t<PlaybackTimeTracker> g_time_tracker;

} // namespace fms
