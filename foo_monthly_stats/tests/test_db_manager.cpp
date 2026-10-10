// Database and playback-time regression tests. These use in-memory SQLite and
// dependency-free identity/timer helpers so they can run without foobar2000.
#include "../catch2/catch_amalgamated.hpp"
#include "../../third_party/sqlite/sqlite3.h"
#include "../track_identity.h"
#include "../playback_time_accumulator.h"

#include <string>
#include <vector>
#include <chrono>
#include <ctime>

struct EventTest
{
    std::string identity;
    std::string path;
    std::string title;
    std::string artist;
    std::string album;
    int64_t played_at = 0;
    int64_t play_count = 0;
    double listened_seconds = 0.0;
};

struct EntryTest
{
    std::string identity;
    std::string title;
    std::string artist;
    std::string album;
    int64_t play_count = 0;
    double listened_seconds = 0.0;
};

static std::string identitySql(const char *alias)
{
    const std::string prefix = std::string(alias) + ".";
    const std::string title = prefix + "title";
    const std::string artist = prefix + "artist";
    const std::string crc = prefix + "track_crc";
    return "CASE WHEN length(trim(COALESCE(" + title + ",'')))>0 AND length(trim(COALESCE(" + artist + ",'')))>0 "
           "THEN 'meta:' || lower(trim(" + title + ")) || char(31) || lower(trim(" + artist + ")) "
           "ELSE 'path:' || COALESCE(" + crc + ",'') END";
}

class TestDb
{
public:
    sqlite3 *db = nullptr;

    bool open(const char *path = ":memory:")
    {
        return sqlite3_open(path, &db) == SQLITE_OK && createSchema();
    }

    bool openLegacy()
    {
        if (sqlite3_open(":memory:", &db) != SQLITE_OK)
            return false;
        const char *sql =
            "CREATE TABLE play_log(id INTEGER PRIMARY KEY,track_crc TEXT,path TEXT,title TEXT,artist TEXT,album TEXT,length_seconds REAL,played_at INTEGER);"
            "CREATE TABLE monthly_count(ymd TEXT,track_crc TEXT,path TEXT,title TEXT,artist TEXT,album TEXT,length_seconds REAL,playcount INTEGER,total_time_seconds REAL,PRIMARY KEY(ymd,track_crc));";
        return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    void close()
    {
        if (db)
        {
            sqlite3_close(db);
            db = nullptr;
        }
    }
    ~TestDb() { close(); }

    bool createSchema()
    {
        const char *sql =
            "CREATE TABLE play_log(id INTEGER PRIMARY KEY,track_crc TEXT NOT NULL,path TEXT NOT NULL DEFAULT '',title TEXT,artist TEXT,album TEXT,length_seconds REAL NOT NULL DEFAULT 0,played_at INTEGER NOT NULL,play_count INTEGER NOT NULL DEFAULT 0,listened_seconds REAL NOT NULL DEFAULT 0);"
            "CREATE TABLE monthly_count(ymd TEXT NOT NULL,track_crc TEXT NOT NULL,path TEXT NOT NULL DEFAULT '',title TEXT,artist TEXT,album TEXT,length_seconds REAL NOT NULL DEFAULT 0,playcount INTEGER NOT NULL DEFAULT 0,total_time_seconds REAL NOT NULL DEFAULT 0,PRIMARY KEY(ymd,track_crc));";
        return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    bool insert(const EventTest &event)
    {
        const std::string ymd = localYmd(event.played_at);
        sqlite3_stmt *stmt = nullptr;
        const char *logSql = "INSERT INTO play_log(track_crc,path,title,artist,album,length_seconds,played_at,play_count,listened_seconds) VALUES(?,?,?,?,?,0,?,?,?)";
        if (sqlite3_prepare_v2(db, logSql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(stmt, 1, event.identity.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, event.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, event.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, event.artist.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, event.album.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 6, event.played_at);
        sqlite3_bind_int64(stmt, 7, event.play_count);
        sqlite3_bind_double(stmt, 8, event.listened_seconds);
        const bool logOk = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!logOk)
            return false;

        const char *countSql =
            "INSERT INTO monthly_count(ymd,track_crc,path,title,artist,album,length_seconds,playcount,total_time_seconds) VALUES(?,?,?,?,?,?,0,?,?) "
            "ON CONFLICT(ymd,track_crc) DO UPDATE SET playcount=playcount+excluded.playcount,total_time_seconds=total_time_seconds+excluded.total_time_seconds,path=excluded.path,title=excluded.title,artist=excluded.artist,album=excluded.album";
        if (sqlite3_prepare_v2(db, countSql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(stmt, 1, ymd.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, event.identity.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, event.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, event.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, event.artist.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, event.album.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 7, event.play_count);
        sqlite3_bind_double(stmt, 8, event.listened_seconds);
        const bool countOk = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return countOk;
    }

    std::vector<EntryTest> query(const std::string &date, char mode)
    {
        std::vector<EntryTest> rows;
        const std::string clause = mode == 'd' ? "c.ymd=?" : mode == 'm' ? "SUBSTR(c.ymd,1,7)=?" : "SUBSTR(c.ymd,1,4)=?";
        const std::string key = identitySql("c");
        const std::string sql = "SELECT " + key + ",MAX(c.title),MAX(c.artist),MAX(c.album),SUM(c.playcount),SUM(c.total_time_seconds) FROM monthly_count c WHERE " + clause + " GROUP BY " + key + " HAVING SUM(c.playcount)>0 OR SUM(c.total_time_seconds)>0 ORDER BY SUM(c.total_time_seconds) DESC";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            return rows;
        sqlite3_bind_text(stmt, 1, date.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            EntryTest entry;
            entry.identity = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            entry.title = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            entry.artist = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
            entry.album = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
            entry.play_count = sqlite3_column_int64(stmt, 4);
            entry.listened_seconds = sqlite3_column_double(stmt, 5);
            rows.push_back(std::move(entry));
        }
        sqlite3_finalize(stmt);
        return rows;
    }

    void refreshMonth(const std::string &month)
    {
        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare_v2(db, "DELETE FROM monthly_count WHERE SUBSTR(ymd,1,7)=?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, month.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        const std::string key = identitySql("play_log");
        const std::string sql = "INSERT INTO monthly_count(ymd,track_crc,path,title,artist,album,length_seconds,playcount,total_time_seconds) "
                                "SELECT strftime('%Y-%m-%d',datetime(played_at/1000,'unixepoch','localtime'))," + key +
                                ",MAX(path),MAX(title),MAX(artist),MAX(album),MAX(length_seconds),SUM(play_count),SUM(listened_seconds) "
                                "FROM play_log WHERE SUBSTR(strftime('%Y-%m-%d',datetime(played_at/1000,'unixepoch','localtime')),1,7)=? "
                                "GROUP BY strftime('%Y-%m-%d',datetime(played_at/1000,'unixepoch','localtime'))," + key +
                                " HAVING SUM(play_count)>0 OR SUM(listened_seconds)>0";
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, month.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    bool migrateLegacy()
    {
        const char *sql =
            "ALTER TABLE play_log ADD COLUMN play_count INTEGER NOT NULL DEFAULT 0;"
            "UPDATE play_log SET play_count=CASE WHEN COALESCE(length_seconds,0)=0 THEN 1 ELSE 0 END;"
            "ALTER TABLE play_log ADD COLUMN listened_seconds REAL NOT NULL DEFAULT 0;"
            "UPDATE monthly_count SET total_time_seconds=0;";
        return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    int64_t rowCount(const char *table)
    {
        sqlite3_stmt *stmt = nullptr;
        const std::string sql = std::string("SELECT COUNT(*) FROM ") + table;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_step(stmt);
        const int64_t count = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return count;
    }

private:
    static std::string localYmd(int64_t timestampMs)
    {
        time_t seconds = static_cast<time_t>(timestampMs / 1000);
        struct tm local{};
#ifdef _WIN32
        localtime_s(&local, &seconds);
#else
        localtime_r(&seconds, &local);
#endif
        char buffer[11] = {};
        strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local);
        return buffer;
    }
};

static int64_t ts(int year, int month, int day)
{
    struct tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = 12;
#ifdef _WIN32
    return static_cast<int64_t>(_mkgmtime(&value)) * 1000;
#else
    return static_cast<int64_t>(timegm(&value)) * 1000;
#endif
}

TEST_CASE("Database creates event and daily/monthly tables", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    REQUIRE(db.rowCount("play_log") == 0);
    REQUIRE(db.rowCount("monthly_count") == 0);
}

TEST_CASE("Short listened tracks remain visible even without a play-count event", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    const auto identity = fms::makeTrackIdentity("Short Song", "Artist", "E:/music/short.flac");
    REQUIRE(db.insert({identity, "E:/music/short.flac", "Short Song", "Artist", "Album", ts(2026, 6, 8), 0, 4.25}));
    const auto rows = db.query("2026-06", 'm');
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].play_count == 0);
    REQUIRE(rows[0].listened_seconds == Catch::Approx(4.25));
}

TEST_CASE("Play-count and listening-time events are independent", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    const auto identity = fms::makeTrackIdentity("Threshold Song", "Artist", "E:/music/threshold.flac");
    REQUIRE(db.insert({identity, "E:/music/threshold.flac", "Threshold Song", "Artist", "Album", ts(2026, 6, 8), 1, 0.0}));
    REQUIRE(db.insert({identity, "E:/music/threshold.flac", "Threshold Song", "Artist", "Album", ts(2026, 6, 8), 0, 8.5}));
    const auto rows = db.query("2026-06-08", 'd');
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].play_count == 1);
    REQUIRE(rows[0].listened_seconds == Catch::Approx(8.5));
}

TEST_CASE("Moved files with the same title and artist merge regardless of album", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    const auto original = fms::makeTrackIdentity("Same Song", "Same Artist", "E:/old/Song.flac");
    const auto moved = fms::makeTrackIdentity(" same song ", "SAME ARTIST", "E:/new/Song.flac");
    REQUIRE(original == moved);
    REQUIRE(db.insert({original, "E:/old/Song.flac", "Same Song", "Same Artist", "Album One", ts(2026, 6, 8), 0, 3.0}));
    REQUIRE(db.insert({moved, "E:/new/Song.flac", " same song ", "SAME ARTIST", "Album Two", ts(2026, 6, 9), 1, 0.0}));
    REQUIRE(db.insert({moved, "E:/new/Song.flac", " same song ", "SAME ARTIST", "Album Two", ts(2026, 6, 9), 0, 2.5}));
    const auto rows = db.query("2026", 'y');
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].play_count == 1);
    REQUIRE(rows[0].listened_seconds == Catch::Approx(5.5));
}

TEST_CASE("Different artists remain separate and untagged files use path identity", "[db]")
{
    const auto first = fms::makeTrackIdentity("Song", "Artist A", "E:/one.flac");
    const auto second = fms::makeTrackIdentity("Song", "Artist B", "E:/two.flac");
    const auto missingOne = fms::makeTrackIdentity("Song", "", "E:/one.flac");
    const auto missingTwo = fms::makeTrackIdentity("Song", "", "E:/two.flac");
    REQUIRE(first != second);
    REQUIRE(missingOne != missingTwo);
}

TEST_CASE("Rebuilding a period sums explicit events instead of counting all log rows as plays", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    const auto identity = fms::makeTrackIdentity("Rebuild", "Artist", "E:/music/rebuild.flac");
    REQUIRE(db.insert({identity, "E:/music/rebuild.flac", "Rebuild", "Artist", "Album", ts(2026, 6, 8), 1, 0.0}));
    REQUIRE(db.insert({identity, "E:/music/rebuild.flac", "Rebuild", "Artist", "Album", ts(2026, 6, 8), 0, 2.0}));
    REQUIRE(db.insert({identity, "E:/music/rebuild.flac", "Rebuild", "Artist", "Album", ts(2026, 6, 8), 0, 5.0}));
    db.refreshMonth("2026-06");
    const auto rows = db.query("2026-06", 'm');
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].play_count == 1);
    REQUIRE(rows[0].listened_seconds == Catch::Approx(7.0));
}


TEST_CASE("Daily, monthly, and yearly totals equal the sum of per-track time", "[db]")
{
    TestDb db;
    REQUIRE(db.open());
    const auto first = fms::makeTrackIdentity("First", "Artist A", "E:/first.flac");
    const auto second = fms::makeTrackIdentity("Second", "Artist B", "E:/second.flac");
    REQUIRE(db.insert({first, "E:/first.flac", "First", "Artist A", "Album", ts(2026, 6, 8), 0, 4.0}));
    REQUIRE(db.insert({first, "E:/first.flac", "First", "Artist A", "Album", ts(2026, 6, 9), 0, 3.0}));
    REQUIRE(db.insert({second, "E:/second.flac", "Second", "Artist B", "Album", ts(2026, 6, 9), 0, 6.0}));

    const auto sumTime = [](const std::vector<EntryTest> &rows)
    {
        double total = 0.0;
        for (const auto &row : rows)
            total += row.listened_seconds;
        return total;
    };
    const auto firstDay = db.query("2026-06-08", 'd');
    const auto secondDay = db.query("2026-06-09", 'd');
    const auto month = db.query("2026-06", 'm');
    const auto year = db.query("2026", 'y');
    REQUIRE(firstDay.size() == 1);
    REQUIRE(secondDay.size() == 2);
    REQUIRE(month.size() == 2);
    REQUIRE(sumTime(firstDay) == Catch::Approx(4.0));
    REQUIRE(sumTime(secondDay) == Catch::Approx(9.0));
    REQUIRE(sumTime(month) == Catch::Approx(13.0));
    REQUIRE(sumTime(year) == Catch::Approx(sumTime(month)));
}

TEST_CASE("Legacy migration retains play counts but starts reliable listening time at zero", "[db]")
{
    TestDb db;
    REQUIRE(db.openLegacy());
    REQUIRE(sqlite3_exec(db.db,
                         "INSERT INTO play_log(track_crc,path,title,artist,album,length_seconds,played_at) VALUES"
                         "('oldcrc','E:/x.flac','Old','Artist','Album',0,1780912800000),"
                         "('oldcrc','E:/x.flac','Old','Artist','Album',12,1780912800000);"
                         "INSERT INTO monthly_count VALUES('2026-06-08','oldcrc','E:/x.flac','Old','Artist','Album',12,1,12);",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(db.migrateLegacy());
    db.refreshMonth("2026-06");
    const auto rows = db.query("2026-06", 'm');
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].play_count == 1);
    REQUIRE(rows[0].listened_seconds == Catch::Approx(0.0));
}

TEST_CASE("Playback timer excludes paused intervals and resets after stop", "[playback]")
{
    using Clock = fms::PlaybackTimeAccumulator::Clock;
    const auto zero = Clock::time_point{};
    fms::PlaybackTimeAccumulator timer;
    timer.resume(zero);
    timer.pause(zero + std::chrono::seconds(5));
    timer.resume(zero + std::chrono::seconds(20));
    REQUIRE(timer.elapsed(zero + std::chrono::seconds(25)) == Catch::Approx(10.0));
    REQUIRE(timer.finish(zero + std::chrono::seconds(30)) == Catch::Approx(15.0));
    REQUIRE(timer.elapsed(zero + std::chrono::seconds(40)) == Catch::Approx(0.0));
}

TEST_CASE("Playback timer preserves fractional short sessions", "[playback]")
{
    using Clock = fms::PlaybackTimeAccumulator::Clock;
    const auto zero = Clock::time_point{};
    fms::PlaybackTimeAccumulator timer;
    timer.resume(zero);
    REQUIRE(timer.finish(zero + std::chrono::milliseconds(2750)) == Catch::Approx(2.75));
}
