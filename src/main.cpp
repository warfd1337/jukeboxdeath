#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/modify/MusicDownloadManager.hpp>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

struct Song {
    std::string id;
    std::string path;
};

int g_songID = 0;
std::string g_currentID;
std::string g_currentPath;

$execute {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
}

void addSongs(const matjson::Value& list, const std::filesystem::path& nongsFolder, std::vector<Song>& songs) {
    if (!list.isArray()) {
        return;
    }
    for (const matjson::Value& item : list) {
        if (!item["unique_id"].isString()) {
            continue;
        }
        std::filesystem::path path;
        if (item["filename"].isString()) {
            path = nongsFolder / item["filename"].asString().unwrap();
        } else if (item["path"].isString()) {
            path = item["path"].asString().unwrap();
        } else {
            continue;
        }
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            continue;
        }
        songs.push_back({item["unique_id"].asString().unwrap(), utils::string::pathToString(path)});
    }
}

std::vector<Song> readSongs(int songID, std::string& activeID) {
    std::vector<Song> songs;
    Mod* jukebox = Loader::get()->getLoadedMod("fleym.nongd");
    if (jukebox == nullptr) {
        return songs;
    }
    std::filesystem::path base(jukebox->getSaveDir().native());
    std::ifstream stream(base / "manifest" / (std::to_string(songID) + ".json"));
    if (!stream.is_open()) {
        return songs;
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    auto parsed = matjson::Value::parse(buffer.str());
    if (parsed.isErr()) {
        return songs;
    }
    matjson::Value json = parsed.unwrap();
    if (json["active"].isString()) {
        activeID = json["active"].asString().unwrap();
    }
    addSongs(json["locals"], base / "nongs", songs);
    addSongs(json["youtube"], base / "nongs", songs);
    addSongs(json["hosted"], base / "nongs", songs);
    return songs;
}

void pickNextSong(int songID) {
    std::string activeID;
    std::vector<Song> songs = readSongs(songID, activeID);
    if (g_currentID.empty()) {
        g_currentID = activeID;
    }
    std::vector<Song> others;
    for (const Song& song : songs) {
        if (song.id != g_currentID) {
            others.push_back(song);
        }
    }
    if (others.empty()) {
        log::info("No other saved NONGs for song {}", songID);
        return;
    }
    Song next = others[std::rand() % others.size()];
    g_songID = songID;
    g_currentID = next.id;
    g_currentPath = next.path;
    log::info("New song: {}", next.path);
}

int getSongKey(GJGameLevel* level) {
    if (level->m_songID != 0) {
        return level->m_songID;
    }
    return -level->m_audioTrack - 1;
}

void resetState() {
    g_songID = 0;
    g_currentID = "";
    g_currentPath = "";
}

class $modify(DeathMusicManager, MusicDownloadManager) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("MusicDownloadManager::pathForSong", Priority::First);
    }

    std::string pathForSong(int id) {
        std::error_code error;
        if (id == g_songID && !g_currentPath.empty() && std::filesystem::exists(g_currentPath, error)) {
            return g_currentPath;
        }
        return MusicDownloadManager::pathForSong(id);
    }
};

class $modify(DeathMusicLevel, GJGameLevel) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("GJGameLevel::getAudioFileName", Priority::First);
    }

    std::string getAudioFileName() {
        std::error_code error;
        if (m_songID == 0 && g_songID == getSongKey(this) && !g_currentPath.empty() && std::filesystem::exists(g_currentPath, error)) {
            return g_currentPath;
        }
        return GJGameLevel::getAudioFileName();
    }
};

class $modify(DeathMusicLayer, PlayLayer) {
    struct Fields {
        bool died = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        resetState();
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool active = Mod::get()->getSettingValue<bool>("mod-active");
        bool allowStartPos = Mod::get()->getSettingValue<bool>("change-on-startpos");
        int64_t minPercent = Mod::get()->getSettingValue<int64_t>("min-percent");
        bool startPosOk = m_startPosObject == nullptr || allowStartPos;
        if (active && startPosOk && object != m_anticheatSpike && this->getCurrentPercent() >= minPercent) {
            m_fields->died = true;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void resetLevel() {
        if (!Mod::get()->getSettingValue<bool>("mod-active")) {
            m_fields->died = false;
            resetState();
        } else if (m_fields->died) {
            m_fields->died = false;
            pickNextSong(getSongKey(m_level));
        }
        PlayLayer::resetLevel();
    }

    void onQuit() {
        resetState();
        PlayLayer::onQuit();
    }
};

class $modify(DeathMusicPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        CCSprite* icon = CCSprite::createWithSpriteFrameName("GJ_musicOnBtn_001.png");
        CCSprite* sprite = nullptr;
        if (icon != nullptr) {
            sprite = CircleButtonSprite::create(icon);
        } else {
            sprite = ButtonSprite::create("DM");
        }
        sprite->setScale(0.8f);

        CCMenuItemSpriteExtra* button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(DeathMusicPause::onDeathMusic)
        );

        CCSize winSize = CCDirector::get()->getWinSize();
        CCMenu* menu = CCMenu::create();
        menu->addChild(button);
        menu->setPosition({winSize.width - 30.f, winSize.height - 30.f});
        this->addChild(menu);
    }

    void onDeathMusic(CCObject*) {
        openSettingsPopup(Mod::get());
    }
};
