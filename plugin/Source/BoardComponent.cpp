#include "BoardComponent.h"
#include "PluginProcessor.h"

BoardComponent::BoardComponent(PlinkoAudioProcessor& p) : proc_(p) {
    board_ = proc_.board();   // start from the processor's current board
    startTimerHz(60);
}
BoardComponent::~BoardComponent() { stopTimer(); }

void BoardComponent::paint(juce::Graphics& g) {
    auto area = boardRect();
    g.fillAll(juce::Colour(0xff15151c));
    g.setColour(juce::Colour(0xff202028));
    g.fillRoundedRectangle(area, 6.0f);

    g.setColour(juce::Colours::dimgrey);
    g.drawLine(sx(0.0f), sy(0.0f), sx(0.0f), sy(board_.topY), 2.0f);
    g.drawLine(sx(board_.width), sy(0.0f), sx(board_.width), sy(board_.topY), 2.0f);

    for (int i = 0; i < board_.pegCount; ++i) {
        float pr = board_.pegRad[i] * scale();   // per-peg size
        float cx = sx(board_.pegX[i]), cy = sy(board_.pegY[i]);
        juce::Colour c = (board_.pegType[i] == 1) ? juce::Colour(0xff5bc0be)   // reverb = teal
                                                  : juce::Colour(0xffe0a458);  // delay  = amber
        g.setColour(c);
        g.fillEllipse(cx - pr, cy - pr, pr * 2.0f, pr * 2.0f);
    }

    float bx = sx(proc_.ballNX.load(std::memory_order_relaxed) * board_.width);
    float by = sy(proc_.ballNY.load(std::memory_order_relaxed) * board_.topY);
    float br = board_.ballRadius * scale();
    g.setColour(juce::Colours::white);
    g.fillEllipse(bx - br, by - br, br * 2.0f, br * 2.0f);

    if (!proc_.running_.load()) {   // stopped: highlight the ball as the draggable start point
        g.setColour(juce::Colour(0xff5bc0be));
        g.drawEllipse(bx - br * 1.6f, by - br * 1.6f, br * 3.2f, br * 3.2f, 2.0f);
    }

    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.setFont(12.0f);
    const char* hint = proc_.running_.load()
        ? "click: add   drag: move   right-click/drag: delete   double-click: delay/reverb"
        : "stopped - drag the ball to set the start point";
    g.drawText(hint, area.reduced(6.0f).removeFromTop(16.0f), juce::Justification::centredLeft);
}

int BoardComponent::pegAt(float bx, float by) const {
    float r = board_.pegRadius * 1.8f;   // a little generous for easy clicking
    int best = -1; float bestD2 = r * r;
    for (int i = 0; i < board_.pegCount; ++i) {
        float dx = bx - board_.pegX[i], dy = by - board_.pegY[i];
        float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

bool BoardComponent::addPeg(float bx, float by) {
    if (board_.pegCount >= 128) return false;
    if (bx < 0.05f || bx > board_.width - 0.05f || by < board_.exitY + 0.05f || by > board_.topY - 0.02f)
        return false;
    int n = board_.pegCount;
    board_.pegX[n] = bx;
    board_.pegY[n] = by;
    board_.pegType[n] = brushType_;                  // active brush decides type...
    board_.pegRest[n] = brushBounce_[brushType_];    // ...bounce...
    board_.pegRad[n]  = brushSize_[brushType_];      // ...and size
    board_.pegCount = n + 1;
    return true;
}

void BoardComponent::removePeg(int i) {
    int last = board_.pegCount - 1;
    board_.pegX[i] = board_.pegX[last];
    board_.pegY[i] = board_.pegY[last];
    board_.pegRest[i] = board_.pegRest[last];
    board_.pegType[i] = board_.pegType[last];
    board_.pegCount = last;
}

void BoardComponent::clearAllPegs() {
    board_.pegCount = 0;
    PlinkoAudioProcessor::Edit ed;
    ed.type = PlinkoAudioProcessor::EditType::Clear;
    proc_.pushEdit(ed);
    repaint();
}

void BoardComponent::revertToDefault() {
    board_ = PlinkoAudioProcessor::defaultBoard();
    PlinkoAudioProcessor::Edit ed;
    ed.type = PlinkoAudioProcessor::EditType::Reset;
    proc_.pushEdit(ed);
    repaint();
}

void BoardComponent::eraseAt(int i) {
    PlinkoAudioProcessor::Edit ed;
    ed.type = PlinkoAudioProcessor::EditType::Delete;
    ed.idx = i;
    proc_.pushEdit(ed);
    removePeg(i);   // mirror locally (same swap-remove the physics will do)
    repaint();
}

void BoardComponent::mouseDown(const juce::MouseEvent& e) {
    float bx = toBoardX(e.position.x), by = toBoardY(e.position.y);

    if (!proc_.running_.load() && !e.mods.isRightButtonDown()) {  // grab the start point
        float dx = bx - board_.dropX, dy = by - board_.dropY, rr = board_.ballRadius * 2.0f;
        if (dx * dx + dy * dy < rr * rr) { dragDrop_ = true; return; }
    }

    int hit = pegAt(bx, by);

    if (e.mods.isRightButtonDown()) {            // right = erase (also sweepable via drag)
        if (hit >= 0) eraseAt(hit);
        return;
    }
    if (hit >= 0) {                              // grab to move
        dragIdx_ = hit;
    } else if (addPeg(bx, by)) {                 // add a delay peg, then drag to position
        int idx = board_.pegCount - 1;
        PlinkoAudioProcessor::Edit ed;
        ed.type = PlinkoAudioProcessor::EditType::Add;
        ed.x = board_.pegX[idx]; ed.y = board_.pegY[idx];
        ed.rest = board_.pegRest[idx]; ed.pegType = board_.pegType[idx];
        ed.radius = board_.pegRad[idx];
        proc_.pushEdit(ed);
        dragIdx_ = idx;
        repaint();
    }
}

void BoardComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragDrop_) {                             // move the start point
        float bx = juce::jlimit(0.05f, board_.width - 0.05f, toBoardX(e.position.x));
        float by = juce::jlimit(board_.exitY + 0.1f, board_.topY - 0.02f, toBoardY(e.position.y));
        board_.dropX = bx; board_.dropY = by;
        PlinkoAudioProcessor::Edit ed;
        ed.type = PlinkoAudioProcessor::EditType::SetDrop;
        ed.x = bx; ed.y = by;
        proc_.pushEdit(ed);
        repaint();
        return;
    }
    if (e.mods.isRightButtonDown()) {            // sweep-erase: delete pegs as the cursor passes
        int hit = pegAt(toBoardX(e.position.x), toBoardY(e.position.y));
        if (hit >= 0) eraseAt(hit);
        return;
    }
    if (dragIdx_ < 0) return;
    float bx = juce::jlimit(0.05f, board_.width - 0.05f, toBoardX(e.position.x));
    float by = juce::jlimit(board_.exitY + 0.05f, board_.topY - 0.02f, toBoardY(e.position.y));
    board_.pegX[dragIdx_] = bx;
    board_.pegY[dragIdx_] = by;
    repaint();   // physics catches up on mouse-up (one Move edit, no ball reset)
}

void BoardComponent::mouseUp(const juce::MouseEvent&) {
    if (dragDrop_) { dragDrop_ = false; return; }
    if (dragIdx_ < 0) return;
    PlinkoAudioProcessor::Edit ed;
    ed.type = PlinkoAudioProcessor::EditType::Move;
    ed.idx = dragIdx_; ed.x = board_.pegX[dragIdx_]; ed.y = board_.pegY[dragIdx_];
    proc_.pushEdit(ed);
    dragIdx_ = -1;
}

void BoardComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    int hit = pegAt(toBoardX(e.position.x), toBoardY(e.position.y));
    if (hit < 0) return;
    board_.pegType[hit] = 1 - board_.pegType[hit];
    PlinkoAudioProcessor::Edit ed;
    ed.type = PlinkoAudioProcessor::EditType::SetType;
    ed.idx = hit; ed.pegType = board_.pegType[hit];
    proc_.pushEdit(ed);
    repaint();
}
