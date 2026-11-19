#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/MusicDownloadManager.hpp>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace geode::prelude;

std::string g_currentSong;

$execute {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
}

std::vector<std::string> getSongs() {
    std::vector<std::string> songs;
    Mod* jukebox = Loader::get()->getLoadedMod("fleym.nongd");
    if (jukebox == nullptr) {
        return songs;
    }
    std::filesystem::path folder = jukebox->getSaveDir() / "nongs";
    std::error_code error;
    if (!std::filesystem::exists(folder, error)) {
        return songs;
    }
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        if (ext == ".mp3" || ext == ".ogg" || ext == ".wav") {
            songs.push_back(utils::string::pathToString(entry.path()));
        }
    }
    return songs;
}

void pickNextSong() {
    std::vector<std::string> songs = getSongs();
    if (songs.empty()) {
        log::warn("Скачанных песен Jukebox не найдено");
        return;
    }
    if (songs.size() == 1) {
        g_currentSong = songs[0];
        return;
    }
    std::string next = g_currentSong;
    while (next == g_currentSong) {
        next = songs[std::rand() % songs.size()];
    }
    g_currentSong = next;
    log::info("Новая песня: {}", g_currentSong);
}

class $modify(DeathMusicManager, MusicDownloadManager) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("MusicDownloadManager::pathForSong", Priority::First);
    }

    std::string pathForSong(int id) {
        std::error_code error;
        if (!g_currentSong.empty() && std::filesystem::exists(g_currentSong, error)) {
            return g_currentSong;
        }
        return MusicDownloadManager::pathForSong(id);
    }
};

class $modify(DeathMusicLayer, PlayLayer) {
    struct Fields {
        bool died = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        g_currentSong = "";
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (object != m_anticheatSpike) {
            m_fields->died = true;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void resetLevel() {
        if (m_fields->died) {
            m_fields->died = false;
            pickNextSong();
        }
        PlayLayer::resetLevel();
    }

    void onQuit() {
        g_currentSong = "";
        PlayLayer::onQuit();
    }
};
