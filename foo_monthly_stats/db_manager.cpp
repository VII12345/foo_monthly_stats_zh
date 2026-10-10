#include "stdafx.h"
#include "db_manager.h"
#include "track_identity.h"

namespace fms
{

    static bool tableExists(sqlite3 *db, const char *table)
    {
        sqlite3_stmt *stmt = nullptr;
        bool exists = false;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, table, -1, SQLITE_STATIC);
            exists = sqlite3_step(stmt) == SQLITE_ROW;
            sqlite3_finalize(stmt);
        }
        return exists;
    }

    static bool hasColumn(sqlite3 *db, const char *table, const char *column)
    {
        const std::string sql = std::string("PRAGMA table_info(") + table + ")";
        sqlite3_stmt *stmt = nullptr;
        bool found = false;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const char *name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                if (name && strcmp(name, column) == 0)
                {
                    found = true;
                    break;
                }
            }
            sqlite3_finalize(stmt);
        }
        return found;
    }

    static std::string columnText(sqlite3_stmt *stmt, int index)
    {
        const auto *value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, index));
        return value ? value : "";
    }

    // Mirrors makeTrackIdentity(): title+artist is stable across file moves;
    // rows without both tags fall back to the legacy path CRC.
    static std::string trackIdentitySql(const char *alias)
    {
        const std::string prefix = std::string(alias) + ".";
        const std::string title = prefix + "title";
        const std::string artist = prefix + "artist";
        const std::string crc = prefix + "track_crc";
        return "CASE WHEN length(trim(COALESCE(" + title + ",'')))>0 AND length(trim(COALESCE(" + artist + ",'')))>0 "
               "THEN 'meta:' || lower(trim(" + title + ")) || char(31) || lower(trim(" + artist + ")) "
               "ELSE 'path:' || COALESCE(" + crc + ",'') END";
    }

    // -----------------------------------------------------------------------
    // DbManager
    // -----------------------------------------------------------------------

    static DbManager g_instance;

    DbManager &DbManager::get() { return g_instance; }

    DbManager::DbManager() = default;

    DbManager::~DbManager() { close(); }

    bool DbManager::open(const char *dbPath)
    {
        if (m_opened)
            return true;

        int rc = sqlite3_open(dbPath, &m_db);
        if (rc != SQLITE_OK)
        {
            FB2K_console_formatter() << "foo_monthly_stats: sqlite3_open failed: " << sqlite3_errmsg(m_db);
            sqlite3_close(m_db);
            m_db = nullptr;
            return false;
        }

        // Performance settings
        sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

        ensureSchema();

        m_running = true;
        m_thread = std::thread(&DbManager::workerThread, this);
        m_opened = true;
        return true;
    }

    void DbManager::close()
    {
        if (!m_opened)
            return;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_running = false;
            m_cv.notify_all();
        }
        if (m_thread.joinable())
            m_thread.join();
        if (m_db)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        m_opened = false;
    }

    void DbManager::postPlay(const TrackInfo &info)
    {
        if (!m_opened)
            return;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_queue.push(info);
        }
        m_cv.notify_one();
    }

    void DbManager::flush()
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cv.wait(lk, [this]
                  { return m_queue.empty() && !m_processing; });
    }

    void DbManager::refreshPeriod(const std::string &period, bool isYear)
    {
        if (!m_db)
            return;
        flush();

        const bool isDay = period.size() == 10;
        const char *deleteSql = isYear
                                    ? "DELETE FROM monthly_count WHERE SUBSTR(ymd,1,4)=?"
                                : isDay
                                    ? "DELETE FROM monthly_count WHERE ymd=?"
                                    : "DELETE FROM monthly_count WHERE SUBSTR(ymd,1,7)=?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, deleteSql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, period.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        const std::string identity = trackIdentitySql("play_log");
        const std::string sql =
            "INSERT INTO monthly_count(ymd,track_crc,path,title,artist,album,length_seconds,playcount,total_time_seconds) "
            "SELECT strftime('%Y-%m-%d',datetime(played_at/1000,'unixepoch','localtime')), " + identity +
            ", MAX(path), MAX(title), MAX(artist), MAX(album), MAX(length_seconds), SUM(play_count), SUM(listened_seconds) "
            "FROM play_log WHERE strftime('%Y-%m-%d',datetime(played_at/1000,'unixepoch','localtime')) LIKE ? ESCAPE '\\' "
            "GROUP BY strftime('%Y-%m-%d',datetime(played_at/1000,'unixepoch','localtime')), " + identity +
            " HAVING SUM(play_count)>0 OR SUM(listened_seconds)>0";

        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            const std::string pattern = isYear ? period + "-%" : isDay ? period : period + "-%";
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        else
        {
            FB2K_console_formatter() << "foo_monthly_stats: refreshPeriod prepare error: " << sqlite3_errmsg(m_db);
        }
    }

    void DbManager::deleteEntry(const std::string &period, bool isYear, bool isDay, const std::string &track_identity)
    {
        if (!m_db)
            return;
        flush();

        const std::string identity = trackIdentitySql("monthly_count");
        const std::string periodClause = isYear ? "SUBSTR(ymd,1,4)=?" : isDay ? "ymd=?" : "SUBSTR(ymd,1,7)=?";
        const std::string sql = "DELETE FROM monthly_count WHERE " + periodClause + " AND " + identity + "=?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, period.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, track_identity.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void DbManager::workerThread()
    {
        while (true)
        {
            TrackInfo item;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait(lk, [this]
                          { return !m_queue.empty() || !m_running; });
                if (!m_running && m_queue.empty())
                    break;
                if (m_queue.empty())
                    continue;
                item = m_queue.front();
                m_queue.pop();
                m_processing = true;
            }
            insertPlay(item);
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_processing = false;
            }
            m_cv.notify_all();
        }
    }

    void DbManager::ensureSchema()
    {
        char *errmsg = nullptr;
        const bool playLogExisted = tableExists(m_db, "play_log");
        const bool hadPlayCount = playLogExisted && hasColumn(m_db, "play_log", "play_count");
        const bool hadListenedSeconds = playLogExisted && hasColumn(m_db, "play_log", "listened_seconds");
        sqlite3_exec(m_db,
                     "CREATE TABLE IF NOT EXISTS play_log ("
                     " id INTEGER PRIMARY KEY, track_crc TEXT NOT NULL, path TEXT NOT NULL DEFAULT '',"
                     " title TEXT, artist TEXT, album TEXT, length_seconds REAL NOT NULL DEFAULT 0,"
                     " played_at INTEGER NOT NULL, play_count INTEGER NOT NULL DEFAULT 0,"
                     " listened_seconds REAL NOT NULL DEFAULT 0);"
                     "CREATE INDEX IF NOT EXISTS ix_played_at ON play_log(played_at);",
                     nullptr, nullptr, &errmsg);
        if (errmsg)
        {
            FB2K_console_formatter() << "foo_monthly_stats: play_log schema error: " << errmsg;
            sqlite3_free(errmsg);
        }

        if (!hasColumn(m_db, "play_log", "path"))
            sqlite3_exec(m_db, "ALTER TABLE play_log ADD COLUMN path TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
        if (!hasColumn(m_db, "play_log", "length_seconds"))
            sqlite3_exec(m_db, "ALTER TABLE play_log ADD COLUMN length_seconds REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
        if (!hasColumn(m_db, "play_log", "play_count"))
            sqlite3_exec(m_db, "ALTER TABLE play_log ADD COLUMN play_count INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
        if (playLogExisted && !hadPlayCount)
        {
            // Legacy zero-duration rows came from on_item_played; positive rows were
            // position snapshots and are deliberately not imported as accurate time.
            sqlite3_exec(m_db, "UPDATE play_log SET play_count=CASE WHEN COALESCE(length_seconds,0)=0 THEN 1 ELSE 0 END", nullptr, nullptr, nullptr);
        }
        if (!hasColumn(m_db, "play_log", "listened_seconds"))
            sqlite3_exec(m_db, "ALTER TABLE play_log ADD COLUMN listened_seconds REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);

        bool hasYmColumn = false;
        bool hasYmdColumn = false;
        {
            sqlite3_stmt *info = nullptr;
            if (sqlite3_prepare_v2(m_db, "PRAGMA table_info(monthly_count)", -1, &info, nullptr) == SQLITE_OK)
            {
                while (sqlite3_step(info) == SQLITE_ROW)
                {
                    const char *column = reinterpret_cast<const char *>(sqlite3_column_text(info, 1));
                    if (!column)
                        continue;
                    if (strcmp(column, "ym") == 0)
                        hasYmColumn = true;
                    if (strcmp(column, "ymd") == 0)
                        hasYmdColumn = true;
                }
                sqlite3_finalize(info);
            }
        }

        if (hasYmColumn && !hasYmdColumn)
        {
            FB2K_console_formatter() << "foo_monthly_stats: migrating monthly_count schema (ym -> ymd) ...";
            sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
            const char *createNew =
                "CREATE TABLE monthly_count_new (ymd TEXT NOT NULL,track_crc TEXT NOT NULL,path TEXT NOT NULL DEFAULT '',"
                "title TEXT,artist TEXT,album TEXT,length_seconds REAL NOT NULL DEFAULT 0,playcount INTEGER NOT NULL DEFAULT 0,"
                "total_time_seconds REAL NOT NULL DEFAULT 0,PRIMARY KEY(ymd,track_crc));";
            if (sqlite3_exec(m_db, createNew, nullptr, nullptr, &errmsg) == SQLITE_OK)
            {
                sqlite3_exec(m_db,
                             "INSERT OR IGNORE INTO monthly_count_new "
                             "SELECT ym||'-01',track_crc,COALESCE(path,''),title,artist,album,"
                             "COALESCE(length_seconds,0),COALESCE(playcount,0),0 FROM monthly_count;",
                             nullptr, nullptr, &errmsg);
                if (errmsg)
                {
                    FB2K_console_formatter() << "foo_monthly_stats: old monthly_count copy error: " << errmsg;
                    sqlite3_free(errmsg);
                    errmsg = nullptr;
                }
                sqlite3_exec(m_db, "DROP TABLE monthly_count;", nullptr, nullptr, nullptr);
                sqlite3_exec(m_db, "ALTER TABLE monthly_count_new RENAME TO monthly_count;", nullptr, nullptr, nullptr);
                sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
            }
            else
            {
                if (errmsg)
                {
                    FB2K_console_formatter() << "foo_monthly_stats: monthly_count migration error: " << errmsg;
                    sqlite3_free(errmsg);
                    errmsg = nullptr;
                }
                sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            }
        }
        else if (!hasYmColumn && !hasYmdColumn)
        {
            sqlite3_exec(m_db,
                         "CREATE TABLE IF NOT EXISTS monthly_count ("
                         " ymd TEXT NOT NULL,track_crc TEXT NOT NULL,path TEXT NOT NULL DEFAULT '',"
                         " title TEXT,artist TEXT,album TEXT,length_seconds REAL NOT NULL DEFAULT 0,"
                         " playcount INTEGER NOT NULL DEFAULT 0,total_time_seconds REAL NOT NULL DEFAULT 0,"
                         " PRIMARY KEY(ymd,track_crc));",
                         nullptr, nullptr, &errmsg);
            if (errmsg)
            {
                FB2K_console_formatter() << "foo_monthly_stats: monthly_count schema error: " << errmsg;
                sqlite3_free(errmsg);
                errmsg = nullptr;
            }
        }

        if (!hasColumn(m_db, "monthly_count", "path"))
            sqlite3_exec(m_db, "ALTER TABLE monthly_count ADD COLUMN path TEXT NOT NULL DEFAULT ''", nullptr, nullptr, nullptr);
        if (!hasColumn(m_db, "monthly_count", "length_seconds"))
            sqlite3_exec(m_db, "ALTER TABLE monthly_count ADD COLUMN length_seconds REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
        if (!hasColumn(m_db, "monthly_count", "playcount"))
            sqlite3_exec(m_db, "ALTER TABLE monthly_count ADD COLUMN playcount INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
        if (!hasColumn(m_db, "monthly_count", "total_time_seconds"))
            sqlite3_exec(m_db, "ALTER TABLE monthly_count ADD COLUMN total_time_seconds REAL NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);

        if (!playLogExisted || !hadListenedSeconds)
        {
            // The old total_time_seconds mixed playback-position snapshots with
            // event counts. Preserve play counts, but start accurate time at zero.
            sqlite3_exec(m_db, "UPDATE monthly_count SET total_time_seconds=0", nullptr, nullptr, nullptr);
        }
    }

    void DbManager::insertPlay(const TrackInfo &info)
    {
        time_t t = static_cast<time_t>(info.played_at / 1000);
        struct tm local_tm{};
#ifdef _WIN32
        localtime_s(&local_tm, &t);
#else
        localtime_r(&t, &local_tm);
#endif
        char ymd[11] = {};
        strftime(ymd, sizeof(ymd), "%Y-%m-%d", &local_tm);

        sqlite3_stmt *stmt = nullptr;
        const char *logSql = "INSERT INTO play_log(track_crc,path,title,artist,album,length_seconds,played_at,play_count,listened_seconds) VALUES(?,?,?,?,?,0,?,?,?)";
        if (sqlite3_prepare_v2(m_db, logSql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            FB2K_console_formatter() << "foo_monthly_stats: play_log prepare error: " << sqlite3_errmsg(m_db);
            return;
        }
        sqlite3_bind_text(stmt, 1, info.track_crc.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, info.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, info.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, info.artist.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, info.album.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 6, info.played_at);
        sqlite3_bind_int64(stmt, 7, info.play_count);
        sqlite3_bind_double(stmt, 8, info.listened_seconds);
        if (sqlite3_step(stmt) != SQLITE_DONE)
            FB2K_console_formatter() << "foo_monthly_stats: play_log insert error: " << sqlite3_errmsg(m_db);
        sqlite3_finalize(stmt);

        const char *countSql =
            "INSERT INTO monthly_count(ymd,track_crc,path,title,artist,album,length_seconds,playcount,total_time_seconds) VALUES(?,?,?,?,?,?,0,?,?) "
            "ON CONFLICT(ymd,track_crc) DO UPDATE SET playcount=playcount+excluded.playcount, "
            "total_time_seconds=total_time_seconds+excluded.total_time_seconds, "
            "path=excluded.path,title=excluded.title,artist=excluded.artist,album=excluded.album";
        if (sqlite3_prepare_v2(m_db, countSql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, ymd, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, info.track_crc.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, info.path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, info.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, info.artist.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, info.album.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 7, info.play_count);
            sqlite3_bind_double(stmt, 8, info.listened_seconds);
            if (sqlite3_step(stmt) != SQLITE_DONE)
                FB2K_console_formatter() << "foo_monthly_stats: monthly_count upsert error: " << sqlite3_errmsg(m_db);
            sqlite3_finalize(stmt);
        }
        else
        {
            FB2K_console_formatter() << "foo_monthly_stats: monthly_count prepare error: " << sqlite3_errmsg(m_db);
        }
    }

    std::vector<MonthlyEntry> DbManager::queryMonth(const std::string &ym)
    {
        std::vector<MonthlyEntry> result;
        if (!m_db)
            return result;

        int year = std::stoi(ym.substr(0, 4));
        int month = std::stoi(ym.substr(5, 2)) - 1;
        if (month == 0)
        {
            month = 12;
            --year;
        }
        char previous[8] = {};
        snprintf(previous, sizeof(previous), "%04d-%02d", year, month);

        const std::string key = trackIdentitySql("c");
        const std::string previousKey = trackIdentitySql("p");
        const std::string sql =
            "SELECT " + key + ",MAX(c.path),MAX(c.title),MAX(c.artist),MAX(c.album),MAX(c.length_seconds),"
            "SUM(c.playcount),SUM(c.total_time_seconds),COALESCE((SELECT SUM(p.playcount) FROM monthly_count p "
            "WHERE SUBSTR(p.ymd,1,7)=? AND " + previousKey + "=" + key + "),0) "
            "FROM monthly_count c WHERE SUBSTR(c.ymd,1,7)=? GROUP BY " + key +
            " HAVING SUM(c.playcount)>0 OR SUM(c.total_time_seconds)>0 "
            "ORDER BY SUM(c.total_time_seconds) DESC,SUM(c.playcount) DESC";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, previous, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, ym.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                MonthlyEntry entry{};
                entry.ymd = ym + "-01";
                entry.track_crc = columnText(stmt, 0);
                entry.path = columnText(stmt, 1);
                entry.title = columnText(stmt, 2);
                entry.artist = columnText(stmt, 3);
                entry.album = columnText(stmt, 4);
                entry.length_seconds = sqlite3_column_double(stmt, 5);
                entry.playcount = sqlite3_column_int64(stmt, 6);
                entry.total_time_seconds = sqlite3_column_double(stmt, 7);
                entry.prev_playcount = sqlite3_column_int64(stmt, 8);
                result.push_back(std::move(entry));
            }
            sqlite3_finalize(stmt);
        }
        else
        {
            FB2K_console_formatter() << "foo_monthly_stats: queryMonth prepare error: " << sqlite3_errmsg(m_db);
        }
        return result;
    }

    std::vector<MonthlyEntry> DbManager::queryDay(const std::string &ymd)
    {
        std::vector<MonthlyEntry> result;
        if (!m_db)
            return result;

        int year = std::stoi(ymd.substr(0, 4));
        int month = std::stoi(ymd.substr(5, 2));
        int day = std::stoi(ymd.substr(8, 2)) - 1;
        if (day == 0)
        {
            --month;
            if (month == 0)
            {
                month = 12;
                --year;
            }
            static const int daysInMonth[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
            const int leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
            day = daysInMonth[month] + (month == 2 ? leap : 0);
        }
        char previous[11] = {};
        snprintf(previous, sizeof(previous), "%04d-%02d-%02d", year, month, day);

        const std::string key = trackIdentitySql("c");
        const std::string previousKey = trackIdentitySql("p");
        const std::string sql =
            "SELECT " + key + ",MAX(c.path),MAX(c.title),MAX(c.artist),MAX(c.album),MAX(c.length_seconds),"
            "SUM(c.playcount),SUM(c.total_time_seconds),COALESCE((SELECT SUM(p.playcount) FROM monthly_count p "
            "WHERE p.ymd=? AND " + previousKey + "=" + key + "),0) "
            "FROM monthly_count c WHERE c.ymd=? GROUP BY " + key +
            " HAVING SUM(c.playcount)>0 OR SUM(c.total_time_seconds)>0 "
            "ORDER BY SUM(c.total_time_seconds) DESC,SUM(c.playcount) DESC";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, previous, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, ymd.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                MonthlyEntry entry{};
                entry.ymd = ymd;
                entry.track_crc = columnText(stmt, 0);
                entry.path = columnText(stmt, 1);
                entry.title = columnText(stmt, 2);
                entry.artist = columnText(stmt, 3);
                entry.album = columnText(stmt, 4);
                entry.length_seconds = sqlite3_column_double(stmt, 5);
                entry.playcount = sqlite3_column_int64(stmt, 6);
                entry.total_time_seconds = sqlite3_column_double(stmt, 7);
                entry.prev_playcount = sqlite3_column_int64(stmt, 8);
                result.push_back(std::move(entry));
            }
            sqlite3_finalize(stmt);
        }
        else
        {
            FB2K_console_formatter() << "foo_monthly_stats: queryDay prepare error: " << sqlite3_errmsg(m_db);
        }
        return result;
    }

    std::vector<MonthlyEntry> DbManager::queryYear(const std::string &year)
    {
        std::vector<MonthlyEntry> result;
        if (!m_db)
            return result;

        const std::string previous = std::to_string(std::stoi(year) - 1);
        const std::string key = trackIdentitySql("c");
        const std::string previousKey = trackIdentitySql("p");
        const std::string sql =
            "SELECT " + key + ",MAX(c.path),MAX(c.title),MAX(c.artist),MAX(c.album),MAX(c.length_seconds),"
            "SUM(c.playcount),SUM(c.total_time_seconds),COALESCE((SELECT SUM(p.playcount) FROM monthly_count p "
            "WHERE SUBSTR(p.ymd,1,4)=? AND " + previousKey + "=" + key + "),0) "
            "FROM monthly_count c WHERE SUBSTR(c.ymd,1,4)=? GROUP BY " + key +
            " HAVING SUM(c.playcount)>0 OR SUM(c.total_time_seconds)>0 "
            "ORDER BY SUM(c.total_time_seconds) DESC,SUM(c.playcount) DESC";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, previous.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, year.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                MonthlyEntry entry{};
                entry.ymd = year + "-01-01";
                entry.track_crc = columnText(stmt, 0);
                entry.path = columnText(stmt, 1);
                entry.title = columnText(stmt, 2);
                entry.artist = columnText(stmt, 3);
                entry.album = columnText(stmt, 4);
                entry.length_seconds = sqlite3_column_double(stmt, 5);
                entry.playcount = sqlite3_column_int64(stmt, 6);
                entry.total_time_seconds = sqlite3_column_double(stmt, 7);
                entry.prev_playcount = sqlite3_column_int64(stmt, 8);
                result.push_back(std::move(entry));
            }
            sqlite3_finalize(stmt);
        }
        else
        {
            FB2K_console_formatter() << "foo_monthly_stats: queryYear prepare error: " << sqlite3_errmsg(m_db);
        }
        return result;
    }

    std::string DbManager::currentYM()
    {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);
        struct tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &t);
#else
        localtime_r(&t, &local_tm);
#endif
        char buf[8];
        strftime(buf, sizeof(buf), "%Y-%m", &local_tm);
        return buf;
    }

    std::string DbManager::currentYMD()
    {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);
        struct tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &t);
#else
        localtime_r(&t, &local_tm);
#endif
        char buf[11];
        strftime(buf, sizeof(buf), "%Y-%m-%d", &local_tm);
        return buf;
    }

    std::string DbManager::currentYear()
    {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);
        struct tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &t);
#else
        localtime_r(&t, &local_tm);
#endif
        char buf[5];
        strftime(buf, sizeof(buf), "%Y", &local_tm);
        return buf;
    }

} // namespace fms
