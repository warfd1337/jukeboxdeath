#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MusicDownloadManager.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Layout.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/SimpleAxisLayout.hpp>
#include <algorithm>
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
    std::string label;
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
        std::string id = item["unique_id"].asString().unwrap();
        std::string label = id;
        if (item["name"].isString()) {
            label = item["name"].asString().unwrap();
            if (item["artist"].isString()) {
                label += " - " + item["artist"].asString().unwrap();
            }
        }
        songs.push_back({id, utils::string::pathToString(path), label});
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

std::string poolKey(int songID) {
    return "pool-" + std::to_string(songID);
}

std::vector<std::string> loadPool(int songID) {
    return Mod::get()->getSavedValue<std::vector<std::string>>(poolKey(songID));
}

bool pickNextSong(int songID) {
    std::string activeID;
    std::vector<Song> songs = readSongs(songID, activeID);
    std::vector<std::string> pool = loadPool(songID);
    if (g_currentID.empty()) {
        g_currentID = activeID;
    }
    std::vector<Song> others;
    for (const Song& song : songs) {
        bool inPool = pool.empty() || std::find(pool.begin(), pool.end(), song.id) != pool.end();
        if (inPool && song.id != g_currentID) {
            others.push_back(song);
        }
    }
    if (others.empty()) {
        log::info("No other NONGs to switch to for song {}", songID);
        return false;
    }
    Song next = others[std::rand() % others.size()];
    g_songID = songID;
    g_currentID = next.id;
    g_currentPath = next.path;
    log::info("New song: {}", next.path);
    return true;
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

class PoolPopup : public geode::Popup {
protected:
    int m_songKey = 0;
    std::vector<Song> m_songs;
    std::vector<bool> m_checked;

    bool init(int songKey) {
        if (!Popup::init(320.f, 240.f)) {
            return false;
        }
        m_songKey = songKey;
        this->setTitle("NONG pool");

        std::string activeID;
        m_songs = readSongs(songKey, activeID);
        std::vector<std::string> pool = loadPool(songKey);
        for (const Song& song : m_songs) {
            bool checked = pool.empty() || std::find(pool.begin(), pool.end(), song.id) != pool.end();
            m_checked.push_back(checked);
        }

        CCSize size = m_mainLayer->getContentSize();
        CCSize listSize = {size.width - 20.f, size.height - 100.f};

        CCScale9Sprite* background = CCScale9Sprite::create("square02b_001.png");
        background->setColor({0, 0, 0});
        background->setOpacity(90);
        background->setScale(0.5f);
        background->setContentSize({listSize.width / 0.5f, listSize.height / 0.5f});
        background->setPosition({size.width / 2.f, 55.f + listSize.height / 2.f});
        m_mainLayer->addChild(background);

        ScrollLayer* list = ScrollLayer::create(listSize);
        list->setPosition({10.f, 55.f});
        for (size_t i = 0; i < m_songs.size(); i++) {
            list->m_contentLayer->addChild(this->createRow(i, listSize.width));
        }
        list->m_contentLayer->setLayout(
            SimpleColumnLayout::create()
                ->setMainAxisDirection(AxisDirection::TopToBottom)
                ->setMainAxisAlignment(MainAxisAlignment::Start)
                ->setMainAxisScaling(AxisScaling::Grow)
                ->setGap(4.f)
        );
        m_mainLayer->addChild(list);

        CCLabelBMFont* hint = CCLabelBMFont::create("Checked NONGs are used when switching. Pick at least 2.", "bigFont.fnt");
        hint->limitLabelWidth(size.width - 30.f, 0.4f, 0.1f);
        hint->setPosition({size.width / 2.f, 44.f});
        m_mainLayer->addChild(hint);

        CCMenu* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        CCMenuItemSpriteExtra* shuffleButton = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Shuffle now"), this, menu_selector(PoolPopup::onShuffle)
        );
        shuffleButton->setPosition({size.width / 2.f, 22.f});
        menu->addChild(shuffleButton);
        m_mainLayer->addChild(menu);

        handleTouchPriority(this);
        return true;
    }

    CCNode* createRow(size_t index, float width) {
        CCNode* row = CCNode::create();
        row->setContentSize({width, 30.f});
        row->setAnchorPoint({0.f, 0.f});

        CCLabelBMFont* label = CCLabelBMFont::create(m_songs[index].label.c_str(), "bigFont.fnt");
        label->limitLabelWidth(width - 70.f, 0.5f, 0.1f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({10.f, 15.f});
        row->addChild(label);

        CCMenu* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        row->addChild(menu);

        CCMenuItemToggler* toggler = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(PoolPopup::onToggle), 0.7f
        );
        toggler->setPosition({width - 25.f, 15.f});
        toggler->setTag(static_cast<int>(index));
        toggler->toggle(m_checked[index]);
        menu->addChild(toggler);
        return row;
    }

    void onToggle(CCObject* sender) {
        CCMenuItemToggler* toggler = static_cast<CCMenuItemToggler*>(sender);
        int index = toggler->getTag();
        m_checked[index] = !toggler->isToggled();
        std::vector<std::string> pool;
        for (size_t i = 0; i < m_songs.size(); i++) {
            if (m_checked[i]) {
                pool.push_back(m_songs[i].id);
            }
        }
        Mod::get()->setSavedValue<std::vector<std::string>>(poolKey(m_songKey), pool);
    }

    void onShuffle(CCObject*) {
        if (pickNextSong(m_songKey)) {
            Notification::create("The next restart will use another NONG", NotificationIcon::Success, 1.5f)->show();
        } else {
            Notification::create("No other NONG to switch to", NotificationIcon::Warning, 1.5f)->show();
        }
    }

public:
    static PoolPopup* create(int songKey) {
        PoolPopup* ret = new PoolPopup();
        if (ret->init(songKey)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

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
        bool useChance = Mod::get()->getSettingValue<bool>("use-chance");
        int64_t chance = Mod::get()->getSettingValue<int64_t>("change-chance");
        int64_t minPercent = Mod::get()->getSettingValue<int64_t>("min-percent");
        bool startPosOk = m_startPosObject == nullptr || allowStartPos;
        if (active && startPosOk && object != m_anticheatSpike) {
            bool passed = false;
            if (useChance) {
                passed = (std::rand() % 100) < chance;
            } else {
                passed = this->getCurrentPercent() >= minPercent;
            }
            if (passed) {
                m_fields->died = true;
            }
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

CCSprite* makeSettingsSprite() {
    CCSprite* icon = CCSprite::createWithSpriteFrameName("GJ_musicOnBtn_001.png");
    CCSprite* sprite = nullptr;
    if (icon != nullptr) {
        sprite = CircleButtonSprite::create(icon);
    } else {
        sprite = ButtonSprite::create("DM");
    }
    sprite->setScale(0.8f);
    return sprite;
}

CCSprite* makePoolSprite() {
    CCSprite* disc = CCSprite::create("JB_PinDisc.png"_spr);
    if (disc != nullptr) {
        disc->setScale(36.f / disc->getContentHeight());
        return disc;
    }
    CCSprite* fallback = ButtonSprite::create("NONG");
    fallback->setScale(0.6f);
    return fallback;
}

class $modify(DeathMusicPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        CCSize winSize = CCDirector::get()->getWinSize();
        bool left = Mod::get()->getSettingValue<bool>("pause-button-left");
        float firstX = left ? 30.f : winSize.width - 30.f;
        float step = left ? 45.f : -45.f;

        CCMenu* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});

        CCMenuItemSpriteExtra* settingsButton = CCMenuItemSpriteExtra::create(
            makeSettingsSprite(), this, menu_selector(DeathMusicPause::onDeathMusic)
        );
        settingsButton->setPosition({firstX, 30.f});
        menu->addChild(settingsButton);

        CCMenuItemSpriteExtra* poolButton = CCMenuItemSpriteExtra::create(
            makePoolSprite(), this, menu_selector(DeathMusicPause::onPool)
        );
        poolButton->setPosition({firstX + step, 30.f});
        menu->addChild(poolButton);

        this->addChild(menu);
    }

    void onDeathMusic(CCObject*) {
        openSettingsPopup(Mod::get());
    }

    void onPool(CCObject*) {
        PlayLayer* layer = PlayLayer::get();
        if (layer == nullptr) {
            return;
        }
        int songKey = getSongKey(layer->m_level);
        std::string activeID;
        if (readSongs(songKey, activeID).empty()) {
            Notification::create("No saved NONGs for this song", NotificationIcon::Warning, 1.5f)->show();
            return;
        }
        PoolPopup::create(songKey)->show();
    }
};
