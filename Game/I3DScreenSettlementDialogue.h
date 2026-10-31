#pragma once
#include "I3DScreen.h"
#include "SettlementMob.h"
#include "SoundTypes.h"
#include <memory>

class CWorldServer;
class CI3DScrollText;
class CI3DScrollBar;

class CI3DScreenSettlementDialogue : public CI3DScreen
{
    struct Command;
    struct Presentation;
    enum class Phase { Topics, Phrase, Summary, Error, Closing };
    CWorldServer *world = nullptr;
    std::uint64_t token = 0;
    int target;
    std::string forced;
    Phase phase = Phase::Topics;
    std::vector<Settlement::DialogueTopic> topics;
    std::vector<CString> labels;
    std::vector<Command> commands;
    std::unique_ptr<Presentation> presentation;
    CI3DScrollText *text = nullptr;
    CI3DScrollBar *topicScroll = nullptr;
    HSOUND voice = SND_EMPTY;
    float phraseTime = 0;
    std::size_t next = 0;
    int offset = 0, speaker = -1;
    bool lips = true;
    std::string resource;
    struct Prop { std::string name; CI3DFigure *figure, *frame, *back; CTexture *texture; };
    std::vector<Prop> props;
    void RedrawTopics();
    void ReadProgram(const Settlement::DialogueTopic &topic);
    void SelectTopic(int index);
    void Advance();
    void Skip();
    void Confirm();
    void StopVoice() noexcept;
    void Fail(const char *detail);
    void ShowProp(const Command &command);
    void HideProp(const std::string &name);
public:
    CI3DScreenSettlementDialogue(CInterface3D *ui, int npc, std::string topic = {});
    ~CI3DScreenSettlementDialogue() override;
    void OnActivate() override;
    void OnDeactivate() override;
    BOOL Draw(CRenderer *renderer) override;
    void OnIdle() override;
    void OnLButtonDown(UINT flags, CPoint point) override;
    void OnKeyDown(UINT key, UINT repeat, UINT flags) override;
};
