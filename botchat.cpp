#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"
#include "bakkesmod/wrappers/includes.h"
#include "imgui/imgui.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class SettingsWindowBase : public BakkesMod::Plugin::PluginSettingsWindow
{
public:
    std::string GetPluginName() override { return "Botchat"; }
    void SetImGuiContext(uintptr_t ctx) override
    {
        ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
    }
};

class BotChat : public BakkesMod::Plugin::BakkesModPlugin,
                public SettingsWindowBase
{
public:
    void onLoad() override;
    void RenderSettings() override;

private:
    struct BotInfo
    {
        PriWrapper pri;
        CarWrapper car;
        Vector pos;
        Vector vel;
        float boost;
        int team;
        uintptr_t addr;
        float ballDist;

        BotInfo(PriWrapper p, CarWrapper c, Vector po, Vector ve, float bo, int te, uintptr_t ad, float bd)
            : pri(p), car(c), pos(po), vel(ve), boost(bo), team(te), addr(ad), ballDist(bd) {}
    };

    struct Situation
    {
        std::vector<BotInfo> bots;
        Vector ballPos{};
        Vector ballVel{};
    };

    void tick();
    void onKickoff();
    void onStatTicker(void* params);
    void onChatMessage(void* params);
    void onMatchEnded();
    bool build(Situation& out);
    std::vector<std::pair<int, float>> candidates(const BotInfo& bot, const Situation& s) const;
    void smartChat(const Situation& s);
    PriWrapper pickBot(int team = -1, uintptr_t exclude = 0, bool requireCar = true);
    bool canChat(uintptr_t address) const;
    void say(PriWrapper bot, int chat, bool reply = false, bool allowCinematic = false);
    void queueReply(PriWrapper speaker, int chat);
    void sayRandom(PriWrapper bot, const std::vector<int>& pool);
    void sayNext(PriWrapper bot, const std::vector<int>& pool, int& counter);
    int localTeam() const;

    std::mt19937 rng{ 1 };
    std::chrono::steady_clock::time_point lastChat{};
    std::unordered_map<uintptr_t, std::chrono::steady_clock::time_point> lastForBot;
    std::unordered_map<uintptr_t, int> lastIndex;
    std::unordered_map<uintptr_t, std::chrono::steady_clock::time_point> lastAt;
    std::unordered_set<uintptr_t> boostAnnounced;
    std::chrono::steady_clock::time_point lastBoost{};
    std::unordered_map<int, int> replyCounters;
    int roundCounter = 0, goalCounter = 0, saveCounter = 0, demoCounter = 0, postMatchCounter = 0;
    bool emitting = false;      // true while we call ChatMessage_TA, so the hook ignores our own chats
    bool postMatchPlayed = false;
    bool goalReplay = false;    // goal replay is playing
    bool podium = false;        // end-of-match podium is showing
};

BAKKESMOD_PLUGIN(BotChat, "Quick-chatting bots", "2.7", PLUGINTYPE_FREEPLAY)

// Game keys for every quick chat. Channel 0 is match chat, 1 is team chat.
// The text is for logs and botchat_echo. Only useful lines are in the pools
// below; the rest stay so quick chats you send are still recognised.
enum ChatId
{
    C_IgotIt = 0, C_NeedBoost, C_TakeTheShot, C_Defending, C_GoForIt, C_Centering,
    C_AllYours, C_InPosition, C_Incoming, C_Faking, C_Bumping, C_OnYourLeft,
    C_OnYourRight, C_Passing, C_RotatingUp, C_RotatingBack, C_YouHaveTime,

    C_NiceShot, C_GreatPass, C_Thanks, C_WhatASave, C_NiceOne, C_WhatAPlay,
    C_GreatClear, C_NiceBlock, C_NiceBump, C_NiceDemo, C_WeGotThis,

    C_OMG, C_Noooo, C_Wow, C_CloseOne, C_NoWay, C_HolyCow, C_Whew, C_Siiiick,
    C_Calculated, C_Savage, C_Okay, C_Yes,

    C_Angry, C_NoProblem, C_Whoops, C_Sorry, C_MyBad, C_Oops, C_MyFault,

    C_GG, C_WellPlayed, C_ThatWasFun, C_Rematch, C_OneMoreGame, C_WhatAGame,
    C_NiceMoves, C_EverybodyDance, C_PartyUp,

    C_GoodLuck, C_HaveFun, C_GetReady, C_ThisIsRL, C_LetsDoThis, C_HereWeGo,
    C_NiceCars, C_IllDoMyBest,

    C_COUNT
};

struct Chat
{
    const char* id;
    const char* text;
    unsigned char channel;
};

static const Chat chats[C_COUNT] = {
    { "Group1Message1",  "I got it!",        1 },
    { "Group1Message2",  "Need Boost!",      1 },
    { "Group1Message3",  "Take the shot!",   1 },
    { "Group1Message4",  "Defending...",     1 },
    { "Group1Message5",  "Go for it!",       1 },
    { "Group1Message6",  "Centering!",       1 },
    { "Group1Message7",  "All yours.",       1 },
    { "Group1Message8",  "In position.",     1 },
    { "Group1Message9",  "Incoming!",        1 },
    { "Group1Message10", "Faking.",          1 },
    { "Group1Message11", "Bumping!",         1 },
    { "Group1Message12", "On your left!",    1 },
    { "Group1Message13", "On your right!",   1 },
    { "Group1Message14", "Passing!",         1 },
    { "Group1Message15", "Rotating Up!",     1 },
    { "Group1Message16", "Rotating back!",   1 },
    { "Group1Message17", "You have time!",   1 },

    { "Group2Message1",  "Nice shot!",       0 },
    { "Group2Message2",  "Great pass!",      0 },
    { "Group2Message3",  "Thanks!",          0 },
    { "Group2Message4",  "What a save!",     0 },
    { "Group2Message5",  "Nice one!",        0 },
    { "Group2Message6",  "What a play!",     0 },
    { "Group2Message7",  "Great clear!",     0 },
    { "Group2Message8",  "Nice block!",      0 },
    { "Group2Message9",  "Nice bump!",       0 },
    { "Group2Message10", "Nice demo!",       0 },
    { "Group2Message11", "We got this.",     0 },

    { "Group3Message1",  "OMG!",             0 },
    { "Group3Message2",  "Noooo!",           0 },
    { "Group3Message3",  "Wow!",             0 },
    { "Group3Message4",  "Close one...",     0 },
    { "Group3Message5",  "No way!",          0 },
    { "Group3Message6",  "Holy cow!",        0 },
    { "Group3Message7",  "Whew.",            0 },
    { "Group3Message8",  "Siiiick!",         0 },
    { "Group3Message9",  "Calculated.",      0 },
    { "Group3Message10", "Savage!",          0 },
    { "Group3Message11", "Okay.",            0 },
    { "Group3Message12", "Yes!",             0 },

    { "Group4Message1",  "$#@%!",            0 },
    { "Group4Message2",  "No problem.",      0 },
    { "Group4Message3",  "Whoops...",        0 },
    { "Group4Message4",  "Sorry!",           0 },
    { "Group4Message5",  "My bad...",        0 },
    { "Group4Message6",  "Oops!",            0 },
    { "Group4Message7",  "My fault.",        0 },

    { "Group5Message1",  "gg",               0 },
    { "Group5Message2",  "Well played.",     0 },
    { "Group5Message3",  "That was fun!",    0 },
    { "Group5Message4",  "Rematch!",         0 },
    { "Group5Message5",  "One. More. Game.", 0 },
    { "Group5Message6",  "What a game!",     0 },
    { "Group5Message7",  "Nice moves!",      0 },
    { "Group5Message8",  "Everybody dance!", 0 },
    { "Group5Message9",  "Party Up?",        0 },

    { "Group6Message1",  "Good luck!",       0 },
    { "Group6Message2",  "Have fun!",        0 },
    { "Group6Message3",  "Get ready.",       0 },
    { "Group6Message4",  "This is Rocket League!", 0 },
    { "Group6Message5",  "Let's do this!",   0 },
    { "Group6Message6",  "Here. We. Go.",    0 },
    { "Group6Message7",  "Nice cars!",       0 },
    { "Group6Message8",  "I'll do my best.", 0 },
};

// Fallback when botchat_smart is off: tactical calls only.
static const std::vector<int> idleChats = {
    C_IgotIt, C_Defending, C_GoForIt, C_Centering, C_AllYours, C_InPosition,
    C_Incoming, C_Faking, C_Bumping, C_Passing, C_RotatingUp, C_RotatingBack
};
static const std::vector<int> kickoffChats = {
    C_GoodLuck, C_GetReady, C_LetsDoThis, C_HereWeGo
};

static const std::vector<int> praiseAfterGoal = { C_NiceShot, C_WhatAPlay, C_GreatPass, C_Siiiick, C_WeGotThis };
static const std::vector<int> praiseAfterSave = { C_WhatASave, C_NiceBlock, C_Siiiick };
static const std::vector<int> praiseAfterDemo = { C_NiceDemo, C_NiceBump, C_WeGotThis };
static const std::vector<int> apologyAfterGoal = { C_MyBad, C_MyFault, C_Sorry, C_Whoops, C_Oops };
static const std::vector<int> angerAfterDemo = { C_Angry, C_Oops, C_Whoops };

// Replies are flagged so they cannot trigger further replies.
static const std::vector<int> replyToApology = { C_NoProblem, C_NoProblem, C_NoProblem };
static const std::vector<int> replyToThanks   = { C_NoProblem, C_WeGotThis };
static const std::vector<int> replyToBoost    = { C_IgotIt, C_YouHaveTime, C_GoForIt };
static const std::vector<int> replyToGotIt    = { C_InPosition, C_AllYours };
static const std::vector<int> replyToIncoming = { C_Defending, C_IgotIt, C_InPosition };
static const std::vector<int> replyToPraise   = { C_Thanks, C_WeGotThis };
static const std::vector<int> replyToSave     = { C_Thanks, C_NiceBlock };
static const std::vector<int> replyToFake     = { C_InPosition, C_NiceOne };
static const std::vector<int> replyToKickoff  = { C_GoodLuck, C_LetsDoThis };
static const std::vector<int> replyToBump     = { C_NiceBump, C_NiceDemo };
static const std::vector<int> replyToEndGame  = { C_GG, C_WellPlayed };

static const std::vector<int>* replyPool(int chat)
{
    switch (chat)
    {
    case C_Sorry: case C_MyBad: case C_MyFault: case C_Whoops: case C_Oops: case C_Angry:
        return &replyToApology;
    case C_Thanks:
        return &replyToThanks;
    case C_NeedBoost:
        return &replyToBoost;
    case C_IgotIt:
        return &replyToGotIt;
    case C_Incoming:
        return &replyToIncoming;
    case C_NiceShot: case C_WhatAPlay: case C_GreatPass: case C_GreatClear: case C_Siiiick: case C_Calculated:
        return &replyToPraise;
    case C_WhatASave: case C_NiceBlock: case C_HolyCow:
        return &replyToSave;
    case C_Faking:
        return &replyToFake;
    case C_Bumping: case C_NiceBump: case C_NiceDemo:
        return &replyToBump;
    case C_GoodLuck: case C_HaveFun: case C_GetReady: case C_ThisIsRL:
    case C_LetsDoThis: case C_HereWeGo: case C_NiceCars: case C_IllDoMyBest:
        return &replyToKickoff;
    default:
        if (chat >= C_GG && chat <= C_PartyUp) return &replyToEndGame;
        return nullptr;
    }
}

struct StatTickerParams
{
    uintptr_t Receiver;
    uintptr_t Victim;
    uintptr_t StatEvent;
};

struct ChatMessageParams
{
    void* InPRI;
    uint8_t Message[0x10];
    unsigned char ChatChannel;
    bool bPreset;
};

static float length(const Vector& v)
{
    return std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
}

static float dot2(const Vector& a, const Vector& b)
{
    return a.X * b.X + a.Y * b.Y;
}

static float distance2D(const Vector& a, const Vector& b)
{
    float dx = a.X - b.X, dy = a.Y - b.Y;
    return std::sqrt(dx * dx + dy * dy);
}

void BotChat::onLoad()
{
    rng.seed(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
             static_cast<unsigned>(GetCurrentProcessId()));

    cvarManager->registerCvar("botchat_enabled", "1", "Bots use quick chats in offline matches", true, true, 0, true, 1);
    cvarManager->registerCvar("botchat_rate", "45", "Percent chance any bot chat is actually sent (global volume knob)", true, true, 0.f, true, 100.f);
    cvarManager->registerCvar("botchat_smart", "1", "Choose quick chats from the game situation instead of at random", true, true, 0, true, 1);
    cvarManager->registerCvar("botchat_alerts", "1", "Allow tactical calls (I got it, defending, need boost...)", true, true, 0, true, 1);
    cvarManager->registerCvar("botchat_repeat", "25", "Seconds before a bot may say the same line again", true, true, 0.f, true, 300.f);
    cvarManager->registerCvar("botchat_replies", "1", "Bots answer each other's quick chats", true, true, 0, true, 1);
    cvarManager->registerCvar("botchat_reply_delay", "2.5", "Seconds before a bot answers a team mate", true, true, 0.f, true, 30.f);
    cvarManager->registerCvar("botchat_listen", "1", "Bots answer your own quick chats too", true, true, 0, true, 1);
    cvarManager->registerCvar("botchat_post_match", "1", "Sportsmanship quick chats at the final whistle", true, true, 0, true, 1);
    cvarManager->registerCvar("botchat_suppress_cinematics", "1", "Stay quiet during goal replays and the podium", true, true, 0, true, 1);
    cvarManager->registerCvar("botchat_events", "1", "Quick chat on goals, saves, demos and kickoffs", true, true, 0, true, 1);
    cvarManager->registerCvar("botchat_boost_threshold", "15", "Boost percent that counts as low", true, true, 0.f, true, 100.f);
    cvarManager->registerCvar("botchat_boost_gap", "35", "Minimum seconds between boost calls from the whole team", true, true, 0.f, true, 300.f);
    cvarManager->registerCvar("botchat_interval", "12", "Seconds between idle quick chats", true, true, 1.f, true, 60.f);
    cvarManager->registerCvar("botchat_chance", "35", "Percent chance an idle chat is considered each interval", true, true, 0.f, true, 100.f);
    cvarManager->registerCvar("botchat_cooldown", "20", "Minimum seconds between two chats from one bot", true, true, 0.f, true, 120.f);
    cvarManager->registerCvar("botchat_gap", "4", "Minimum seconds between any two bot chats", true, true, 0.f, true, 60.f);
    cvarManager->registerCvar("botchat_team_info", "1", "Tactical quick chats go to team chat", true, true, 0, true, 1);
    cvarManager->registerCvar("botchat_match_chat", "1", "Allow match-chat quick chats", true, true, 0, true, 1);
    cvarManager->registerCvar("botchat_team_chat", "1", "Allow team-chat quick chats", true, true, 0, true, 1);
    cvarManager->registerCvar("botchat_echo", "0", "Also print the chat in BakkesMod's chat box", true, true, 0, true, 1);

    gameWrapper->HookEvent("Function GameEvent_Soccar_TA.Countdown.BeginState", [this](std::string) {
        lastForBot.clear();
        lastIndex.clear();
        boostAnnounced.clear();
        postMatchPlayed = false;
        goalReplay = false;
        podium = false;
        gameWrapper->SetTimeout([this](GameWrapper*) { onKickoff(); }, 1.0f);
    });

    gameWrapper->HookEventWithCallerPost<ServerWrapper>("Function TAGame.GFxHUD_TA.HandleStatTickerMessage",
        [this](ServerWrapper, void* params, std::string) { onStatTicker(params); });

    gameWrapper->HookEventWithCallerPost<PlayerControllerWrapper>("Function TAGame.PlayerController_TA.ChatMessage_TA",
        [this](PlayerControllerWrapper, void* params, std::string) { onChatMessage(params); });

    gameWrapper->HookEvent("Function GameEvent_Soccar_TA.ReplayPlayback.BeginState", [this](std::string) { goalReplay = true; });
    gameWrapper->HookEvent("Function GameEvent_Soccar_TA.ReplayPlayback.EndState", [this](std::string) { goalReplay = false; });

    gameWrapper->HookEvent("Function TAGame.GameEvent_Soccar_TA.OnMatchEnded", [this](std::string) { onMatchEnded(); });
    gameWrapper->HookEvent("Function TAGame.GameEvent_Soccar_TA.EventMatchEnded", [this](std::string) { onMatchEnded(); });

    tick();
}

int BotChat::localTeam() const
{
    PlayerControllerWrapper pc = gameWrapper->GetPlayerController();
    if (!pc) return -1;
    PriWrapper local = pc.GetPRI();
    if (!local || local.GetTeamNum() == 255) return -1; // spectator
    return static_cast<int>(local.GetTeamNum());
}

bool BotChat::build(Situation& out)
{
    ServerWrapper game = gameWrapper->GetCurrentGameState();
    if (!game) return false;
    BallWrapper ball = game.GetBall();
    if (!ball) return false;
    out.ballPos = ball.GetLocation();
    out.ballVel = ball.GetVelocity();
    out.bots.clear();

    auto pris = game.GetPRIs();
    for (int i = 0; i < pris.Count(); ++i)
    {
        PriWrapper pri = pris.Get(i);
        if (!pri || !pri.GetbBot() || pri.IsSpectator()) continue;
        CarWrapper car = pri.GetCar();
        if (!car) continue;
        BoostWrapper boost = car.GetBoostComponent();
        Vector pos = car.GetLocation();
        out.bots.emplace_back(pri, car, pos, car.GetVelocity(),
                              boost ? boost.GetPercentBoostFull() : 0.f,
                              static_cast<int>(pri.GetTeamNum()), pri.memory_address,
                              distance2D(pos, out.ballPos));
    }
    return !out.bots.empty();
}

std::vector<std::pair<int, float>> BotChat::candidates(const BotInfo& b, const Situation& s) const
{
    std::vector<std::pair<int, float>> out;
    auto add = [&](int chat, float weight) { if (weight > 0.05f) out.emplace_back(chat, weight); };

    bool alerts = cvarManager->getCvar("botchat_alerts").getBoolValue();

    float mateDist = 1e9f, foeDist = 1e9f;
    bool teammateCloser = false;
    for (const BotInfo& other : s.bots)
    {
        if (other.addr == b.addr) continue;
        if (other.team == b.team)
        {
            mateDist = std::min(mateDist, other.ballDist);
            if (other.ballDist < b.ballDist - 300.f) teammateCloser = true;
        }
        else foeDist = std::min(foeDist, other.ballDist);
    }

    // Blue defends -X, orange defends +X. "sign" points toward the opponent goal.
    float ownGoalX = (b.team == 0) ? -4096.f : 4096.f;
    float sign = (b.team == 0) ? 1.f : -1.f;
    bool ballOwnHalf = (s.ballPos.X * sign) < 0.f;
    float ownGoalDist = std::abs(b.pos.X - ownGoalX);
    float ballSpeed = length(s.ballVel);
    Vector towardOwnGoal{ ownGoalX - s.ballPos.X, -s.ballPos.Y, 0.f };

    // Boost is rate-limited in tick(), so it stays out of this picker.
    if (alerts)
    {
        if (b.ballDist < 1600.f && !teammateCloser)
            add(C_IgotIt, 5.f * (1.f - b.ballDist / 1600.f));

        if (teammateCloser && mateDist < 1400.f)
        {
            add(C_AllYours, 4.5f);
            add(C_TakeTheShot, 2.5f);
            add(C_GoForIt, 1.5f);
        }

        if (ballOwnHalf)
        {
            if ((b.pos.X - s.ballPos.X) * sign < 200.f) add(C_Defending, 3.f);
            add(C_InPosition, 2.f);
        }

        if (ballSpeed > 700.f && dot2(s.ballVel, towardOwnGoal) > 0.f && ownGoalDist < 2600.f)
            add(C_Incoming, 3.5f);

        if (s.ballPos.Z > 200.f && b.ballDist < 1800.f && foeDist < 900.f)
        {
            Vector toBall{ s.ballPos.X - b.pos.X, s.ballPos.Y - b.pos.Y, 0.f };
            if (dot2(b.vel, toBall) < 0.f) add(C_Faking, 3.f);
        }

        if (b.ballDist < 900.f)
        {
            Vector toOppGoal{ -ownGoalX - b.pos.X, -b.pos.Y, 0.f };
            if (dot2(b.vel, toOppGoal) > 100.f) add(C_Centering, 2.5f);
            if (mateDist < 1600.f) add(C_Passing, 2.f);
        }

        for (const BotInfo& other : s.bots)
        {
            if (other.team == b.team) continue;
            if (distance2D(b.pos, other.pos) < 450.f && length(b.vel) > 900.f)
            {
                add(C_Bumping, 2.f);
                break;
            }
        }

        if (ballOwnHalf && b.pos.X * sign < -1500.f) add(C_RotatingBack, 1.5f);
        if (!ballOwnHalf && b.pos.X * sign > 500.f) add(C_RotatingUp, 1.2f);
        if (b.ballDist < 1200.f && foeDist > 2500.f && ballSpeed < 300.f) add(C_YouHaveTime, 1.f);

        for (const BotInfo& other : s.bots)
        {
            if (other.team != b.team || other.addr == b.addr) continue;
            if (distance2D(b.pos, other.pos) < 900.f)
            {
                float side = (b.pos.Y - other.pos.Y);
                if (side > 150.f) add(C_OnYourLeft, 1.2f);
                else if (side < -150.f) add(C_OnYourRight, 1.2f);
                break;
            }
        }
    }

    return out;
}

void BotChat::smartChat(const Situation& s)
{
    struct Choice
    {
        uintptr_t addr;
        int chat;
        float score;
        PriWrapper pri;
    };

    using namespace std::chrono;
    auto now = steady_clock::now();
    float repeatWindow = cvarManager->getCvar("botchat_repeat").getFloatValue();

    std::vector<Choice> choices;
    for (const BotInfo& b : s.bots)
        for (const auto& candidate : candidates(b, s))
        {
            float weight = candidate.second;
            auto it = lastIndex.find(b.addr);
            if (it != lastIndex.end() && it->second == candidate.first)
            {
                auto at = lastAt.find(b.addr);
                if (at != lastAt.end() && duration_cast<seconds>(now - at->second).count() < static_cast<long long>(repeatWindow))
                    weight *= 0.12f; // strongly discourage saying the same thing again
            }
            // A small jitter only breaks ties between similarly fitting lines.
            float jitter = 0.85f + 0.30f * static_cast<float>(rng() % 1000) / 1000.f;
            choices.push_back({ b.addr, candidate.first, weight * jitter, b.pri });
        }

    if (choices.empty()) return;
    std::sort(choices.begin(), choices.end(), [](const Choice& a, const Choice& b) { return a.score > b.score; });

    float top = choices.front().score;
    std::vector<Choice> shortlist;
    for (const Choice& c : choices)
    {
        if (c.score < top * 0.6f || shortlist.size() >= 4) break;
        shortlist.push_back(c);
    }

    float total = 0.f;
    for (const Choice& c : shortlist) total += c.score;
    float roll = static_cast<float>(rng() % 10000) / 10000.f * total;
    for (const Choice& c : shortlist)
    {
        roll -= c.score;
        if (roll <= 0.f) { say(c.pri, c.chat); return; }
    }
    say(shortlist.front().pri, shortlist.front().chat);
}

PriWrapper BotChat::pickBot(int team, uintptr_t exclude, bool requireCar)
{
    ServerWrapper game = gameWrapper->GetCurrentGameState();
    if (!game) return PriWrapper(0);
    auto pris = game.GetPRIs();
    std::vector<PriWrapper> bots;
    for (int i = 0; i < pris.Count(); ++i)
    {
        PriWrapper pri = pris.Get(i);
        if (!pri || !pri.GetbBot() || pri.IsSpectator()) continue;
        if (requireCar && !pri.GetCar()) continue;
        if (exclude && pri.memory_address == exclude) continue;
        if (team >= 0 && static_cast<int>(pri.GetTeamNum()) != team) continue;
        bots.push_back(pri);
    }
    if (bots.empty()) return PriWrapper(0);
    return bots[rng() % bots.size()];
}

bool BotChat::canChat(uintptr_t address) const
{
    using namespace std::chrono;
    auto now = steady_clock::now();
    float gap = cvarManager->getCvar("botchat_gap").getFloatValue();
    if (duration_cast<milliseconds>(now - lastChat).count() < static_cast<long long>(gap * 1000.f)) return false;
    float cooldown = cvarManager->getCvar("botchat_cooldown").getFloatValue();
    auto it = lastForBot.find(address);
    if (it != lastForBot.end() && duration_cast<milliseconds>(now - it->second).count() < static_cast<long long>(cooldown * 1000.f)) return false;
    return true;
}

void BotChat::say(PriWrapper bot, int index, bool reply, bool allowCinematic)
{
    if (!bot || index < 0 || index >= C_COUNT) return;
    if (gameWrapper->IsInOnlineGame() || gameWrapper->IsInReplay()) return;

    // The post-match sequence passes allowCinematic deliberately.
    if (!allowCinematic && cvarManager->getCvar("botchat_suppress_cinematics").getBoolValue() && (goalReplay || podium))
        return;

    // One rate gate for all messages, replies included.
    float rate = cvarManager->getCvar("botchat_rate").getFloatValue();
    if (static_cast<float>(rng() % 1000) / 10.f >= rate) return;

    PlayerControllerWrapper pc = gameWrapper->GetPlayerController();
    if (!pc) return;

    const Chat& c = chats[index];
    unsigned char channel = c.channel;
    if (channel == 1 && !cvarManager->getCvar("botchat_team_info").getBoolValue()) channel = 0;
    if (channel == 1 && !cvarManager->getCvar("botchat_team_chat").getBoolValue()) return;
    if (channel == 0 && !cvarManager->getCvar("botchat_match_chat").getBoolValue()) return;

    // bPreset marks a quick chat rather than typed text. Calling this on the
    // local controller with the bot as sender is how the game delivers a chat
    // received over the network.
    emitting = true;
    pc.ChatMessage_TA(bot, std::string(c.id), channel, 1);
    emitting = false;

    if (cvarManager->getCvar("botchat_echo").getBoolValue())
        gameWrapper->LogToChatbox(bot.GetPlayerName().ToString() + ": " + c.text, "BOTCHAT");

    auto now = std::chrono::steady_clock::now();
    lastChat = now;
    lastForBot[bot.memory_address] = now;
    lastIndex[bot.memory_address] = index;
    lastAt[bot.memory_address] = now;
    cvarManager->log("botchat: " + bot.GetPlayerName().ToString() + " -> " + c.text + (channel == 1 ? " [team]" : ""));

    if (!reply) queueReply(bot, index);
}

// Answers rotate and are flagged as replies so they cannot spawn more replies.
void BotChat::queueReply(PriWrapper speaker, int chat)
{
    if (!cvarManager->getCvar("botchat_replies").getBoolValue()) return;
    const std::vector<int>* pool = replyPool(chat);
    if (!pool || pool->empty()) return;

    int team = static_cast<int>(speaker.GetTeamNum());
    bool fromOpponent = (chat >= C_GG && chat <= C_PartyUp);
    int responderTeam = fromOpponent ? (team == 0 ? 1 : 0) : team;
    if (responderTeam == 255) responderTeam = -1;

    int& counter = replyCounters[chat];
    int answer = (*pool)[counter % pool->size()];
    ++counter;

    float delay = cvarManager->getCvar("botchat_reply_delay").getFloatValue();
    uintptr_t exclude = speaker.memory_address;
    gameWrapper->SetTimeout([this, responderTeam, exclude, answer](GameWrapper*) {
        PriWrapper responder = pickBot(responderTeam, exclude);
        if (responder) say(responder, answer, true);
    }, delay);
}

void BotChat::sayRandom(PriWrapper bot, const std::vector<int>& pool)
{
    if (!bot || pool.empty() || !canChat(bot.memory_address)) return;
    say(bot, pool[rng() % pool.size()]);
}

// Rotates so an event doesn't repeat the same reaction.
void BotChat::sayNext(PriWrapper bot, const std::vector<int>& pool, int& counter)
{
    if (!bot || pool.empty() || !canChat(bot.memory_address)) return;
    counter++;
    say(bot, pool[counter % pool.size()]);
}

void BotChat::tick()
{
    if (cvarManager->getCvar("botchat_enabled").getBoolValue() &&
        !gameWrapper->IsInOnlineGame() && !gameWrapper->IsInReplay())
    {
        Situation s;
        if (build(s))
        {
            // Boost is checked every tick, but a bot asks only once per dry
            // spell and the team as a whole asks at most once per boost_gap.
            if (cvarManager->getCvar("botchat_alerts").getBoolValue())
            {
                using namespace std::chrono;
                float threshold = cvarManager->getCvar("botchat_boost_threshold").getFloatValue();
                float boostGap = cvarManager->getCvar("botchat_boost_gap").getFloatValue();
                for (const BotInfo& b : s.bots)
                {
                    if (b.boost > threshold + 15.f) boostAnnounced.erase(b.addr); // refilled: re-arm
                }
                bool teamCanAsk = duration_cast<seconds>(steady_clock::now() - lastBoost).count() >= static_cast<long long>(boostGap);
                if (teamCanAsk)
                {
                    for (const BotInfo& b : s.bots)
                    {
                        if (b.boost >= threshold || boostAnnounced.count(b.addr)) continue;
                        if (canChat(b.addr))
                        {
                            say(b.pri, C_NeedBoost);
                            boostAnnounced.insert(b.addr);
                            lastBoost = std::chrono::steady_clock::now();
                        }
                        break;
                    }
                }
            }

            using namespace std::chrono;
            float interval = cvarManager->getCvar("botchat_interval").getFloatValue();
            float chance = cvarManager->getCvar("botchat_chance").getFloatValue();
            if (duration_cast<milliseconds>(steady_clock::now() - lastChat).count() >= static_cast<long long>(interval * 1000.f) &&
                static_cast<float>(rng() % 1000) / 10.f < chance)
            {
                if (cvarManager->getCvar("botchat_smart").getBoolValue()) smartChat(s);
                else sayRandom(pickBot(), idleChats);
            }
        }
    }
    gameWrapper->SetTimeout([this](GameWrapper*) { tick(); }, 1.0f);
}

void BotChat::onKickoff()
{
    if (!cvarManager->getCvar("botchat_enabled").getBoolValue()) return;
    if (!cvarManager->getCvar("botchat_events").getBoolValue()) return;

    Situation s;
    if (!build(s)) return;

    const BotInfo* closest = nullptr;
    for (const BotInfo& b : s.bots)
        if (!closest || b.ballDist < closest->ballDist) closest = &b;
    if (!closest) return;
    say(closest->pri, C_IgotIt);

    roundCounter++;
    int line = kickoffChats[roundCounter % kickoffChats.size()];
    gameWrapper->SetTimeout([this, line](GameWrapper*) {
        Situation s2;
        if (!build(s2)) return;
        for (const BotInfo& b : s2.bots)
        {
            if (b.ballDist > 400.f) { say(b.pri, line); return; }
        }
    }, 2.5f);
}

void BotChat::onStatTicker(void* params)
{
    if (!cvarManager->getCvar("botchat_enabled").getBoolValue()) return;
    if (!cvarManager->getCvar("botchat_events").getBoolValue()) return;
    if (!params) return;

    auto* p = static_cast<StatTickerParams*>(params);
    if (!p->StatEvent) return;
    StatEventWrapper stat(p->StatEvent);
    std::string name = stat.GetEventName();
    PriWrapper receiver(p->Receiver);
    PriWrapper victim(p->Victim);

    if (name == "Goal")
    {
        if (receiver && receiver.GetbBot())
        {
            PriWrapper mate = pickBot(static_cast<int>(receiver.GetTeamNum()), receiver.memory_address);
            if (mate) sayNext(mate, praiseAfterGoal, goalCounter);
            else sayNext(receiver, { C_Calculated, C_NiceOne }, goalCounter);
        }
        else
        {
            PriWrapper mate = pickBot(localTeam());
            if (mate) sayNext(mate, praiseAfterGoal, goalCounter);
        }
    }
    else if (name == "OwnGoal")
    {
        Situation s;
        if (build(s))
        {
            const BotInfo* culprit = nullptr;
            float best = 1e9f;
            for (const BotInfo& b : s.bots)
            {
                float ownGoalX = (b.team == 0) ? -4096.f : 4096.f;
                float d = std::abs(b.pos.X - ownGoalX);
                if (d < best) { best = d; culprit = &b; }
            }
            if (culprit) sayNext(culprit->pri, apologyAfterGoal, goalCounter);
        }
    }
    else if (name == "Save")
    {
        if (receiver && receiver.GetbBot())
        {
            PriWrapper mate = pickBot(static_cast<int>(receiver.GetTeamNum()), receiver.memory_address);
            if (mate) sayNext(mate, praiseAfterSave, saveCounter);
            else sayNext(receiver, { C_NiceBlock, C_WhatASave }, saveCounter);
        }
    }
    else if (name == "Demolish")
    {
        if (victim && victim.GetbBot()) sayNext(victim, angerAfterDemo, demoCounter);
        else if (receiver && receiver.GetbBot())
        {
            PriWrapper mate = pickBot(static_cast<int>(receiver.GetTeamNum()), receiver.memory_address);
            sayNext(mate ? mate : receiver, praiseAfterDemo, demoCounter);
        }
    }
    else if (name == "Center")
    {
        if (receiver && receiver.GetbBot()) sayNext(receiver, { C_Centering, C_Passing }, saveCounter);
    }
}

void BotChat::RenderSettings()
{
    auto toggle = [this](const char* label, const char* name, const char* tip)
    {
        CVarWrapper cvar = cvarManager->getCvar(name);
        if (!cvar) return;
        bool value = cvar.getBoolValue();
        if (ImGui::Checkbox(label, &value)) cvar.setValue(value);
        if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };
    auto slider = [this](const char* label, const char* name, float min, float max, const char* fmt, const char* tip)
    {
        CVarWrapper cvar = cvarManager->getCvar(name);
        if (!cvar) return;
        float value = cvar.getFloatValue();
        if (ImGui::SliderFloat(label, &value, min, max, fmt)) cvar.setValue(value);
        if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };

    if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen))
    {
        toggle("Enable bot chatter", "botchat_enabled", "Master switch for the whole plugin");
        slider("Overall chat chance", "botchat_rate", 0.f, 100.f, "%.0f%%",
               "Global chance that any message is sent. Lower = quieter matches.");
        toggle("Situation-aware lines", "botchat_smart", "Pick lines from the game state instead of at random");
        toggle("Tactical alerts", "botchat_alerts", "I got it, defending, need boost and similar calls");
        toggle("Bots answer bots", "botchat_replies", "A team mate answers after a quick chat");
        toggle("Answer your chats", "botchat_listen", "Bots also answer quick chats you send");
        toggle("Event reactions", "botchat_events", "React to kickoffs, goals, saves, demos and centers");
        toggle("End-of-match exchange", "botchat_post_match", "gg / Well played at the final whistle");
        toggle("Quiet in replays and podium", "botchat_suppress_cinematics",
               "Hold all chatter while a goal replay or the podium is showing");
        toggle("Echo in chat box", "botchat_echo", "Also print the lines in BakkesMod's chat box");
    }

    if (ImGui::CollapsingHeader("Timing"))
    {
        slider("Repeat delay", "botchat_repeat", 0.f, 300.f, "%.0f s",
               "Seconds before the same bot may say the same line again");
        slider("Reply delay", "botchat_reply_delay", 0.f, 30.f, "%.1f s",
               "Seconds before a bot answers a team mate");
        slider("Idle interval", "botchat_interval", 1.f, 60.f, "%.0f s",
               "Seconds between chances for an idle call");
        slider("Idle chance", "botchat_chance", 0.f, 100.f, "%.0f%%",
               "Percent chance an idle call is considered each interval");
        slider("Per-bot cooldown", "botchat_cooldown", 0.f, 120.f, "%.0f s",
               "Minimum seconds between two chats from one bot");
        slider("Gap between chats", "botchat_gap", 0.f, 60.f, "%.0f s",
               "Minimum seconds between any two bot chats");
        slider("Boost-call gap", "botchat_boost_gap", 0.f, 300.f, "%.0f s",
               "Minimum seconds between boost calls from the whole team");
    }

    if (ImGui::CollapsingHeader("Boost and chat channels"))
    {
        slider("Low boost below", "botchat_boost_threshold", 0.f, 100.f, "%.0f%%",
               "Boost percent at which a bot asks for boost");
        toggle("Tactical as team chat", "botchat_team_info",
               "Tactical calls go to team chat instead of match chat");
        toggle("Allow match chat", "botchat_match_chat", "Allow match-chat quick chats");
        toggle("Allow team chat", "botchat_team_chat", "Allow team-chat quick chats");
    }

    ImGui::Spacing();
    ImGui::TextWrapped("Bots only talk in offline matches. Settings are saved automatically.");
}

void BotChat::onChatMessage(void* params)
{
    if (emitting) return;
    if (!cvarManager->getCvar("botchat_enabled").getBoolValue()) return;
    if (!cvarManager->getCvar("botchat_replies").getBoolValue()) return;
    if (!cvarManager->getCvar("botchat_listen").getBoolValue()) return;
    if (!params) return;

    auto* p = static_cast<ChatMessageParams*>(params);
    if (!p->InPRI || !p->bPreset) return; // preset = quick chat, not typed text

    PriWrapper sender(reinterpret_cast<uintptr_t>(p->InPRI));
    if (!sender || sender.GetbBot() || sender.IsSpectator()) return; // humans only

    std::string message = UnrealStringWrapper(reinterpret_cast<uintptr_t>(&p->Message)).ToString();
    for (int i = 0; i < C_COUNT; ++i)
    {
        if (message == chats[i].id)
        {
            queueReply(sender, i);
            return;
        }
    }
}

void BotChat::onMatchEnded()
{
    podium = true; // hold normal chatter while the podium is showing
    if (!cvarManager->getCvar("botchat_enabled").getBoolValue()) return;
    if (!cvarManager->getCvar("botchat_post_match").getBoolValue()) return;
    if (postMatchPlayed) return;
    postMatchPlayed = true;

    ServerWrapper game = gameWrapper->GetCurrentGameState();
    if (!game) return;

    int winner = -1, loser = -1, best = -1, worst = 1000000;
    auto teams = game.GetTeams();
    for (int i = 0; i < teams.Count(); ++i)
    {
        TeamWrapper team = teams.Get(i);
        if (!team) continue;
        int score = team.GetScore();
        int num = static_cast<int>(team.GetTeamNum());
        if (score > best) { best = score; winner = num; }
        if (score < worst) { worst = score; loser = num; }
    }
    if (winner < 0 || loser < 0 || winner == loser) { winner = 0; loser = 1; }

    postMatchCounter++;
    int opening = (postMatchCounter % 2 == 0) ? C_GG : C_WellPlayed;
    int closing = (postMatchCounter / 2) % 3;
    int lastLine = (closing % 2 == 0) ? C_ThatWasFun : C_WhatAGame;

    gameWrapper->SetTimeout([this, opening, winner](GameWrapper*) {
        PriWrapper bot = pickBot(winner, 0, false);
        if (bot) say(bot, opening, true, true);
    }, 1.5f);

    gameWrapper->SetTimeout([this, loser](GameWrapper*) {
        PriWrapper bot = pickBot(loser, 0, false);
        if (bot) say(bot, C_WellPlayed, true, true);
    }, 4.0f);

    gameWrapper->SetTimeout([this, lastLine, winner](GameWrapper*) {
        PriWrapper bot = pickBot(winner, 0, false);
        if (!bot) bot = pickBot(-1, 0, false);
        if (bot) say(bot, lastLine, true, true);
    }, 6.5f);
}
