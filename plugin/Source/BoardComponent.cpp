#include "BoardComponent.h"
#include "PluginProcessor.h"

BoardComponent::BoardComponent(PlinkoAudioProcessor& p) : proc_(p) {
    board_ = proc_.board();   // start from the processor's current board
    startTimerHz(60);
}
BoardComponent::~BoardComponent() { stopTimer(); }

void BoardComponent::paint(juce::Graphics& g) {
    auto area = boardArea();
    g.fillAll(juce::Colour(0xff15151c));
    g.setColour(juce::Colour(0xff202028));
    g.fillRoundedRectangle(area, 6.0f);

    g.setColour(juce::Colours::dimgrey);
    g.drawLine(sx(0.0f), sy(0.0f), sx(0.0f), sy(board_.topY), 2.0f);
    g.drawLine(sx(board_.width), sy(0.0f), sx(board_.width), sy(board_.topY), 2.0f);

    float pr = (board_.pegRadius / board_.width) * area.getWidth();
    for (int i = 0; i < board_.pegCount; ++i) {
        float cx = sx(board_.pegX[i]), cy = sy(board_.pegY[i]);
        juce::Colour c = (board_.pegType[i] == 1) ? juce::Colour(0xff5bc0be)   // reverb = teal
                                                  : juce::Colour(0xffe0a458);  // delay  = amber
        if (board_.pegRest[i] > 1.0f) c = juce::Colour(0xffff4d6d);            // bumper = red
        g.setColour(c);
        g.fillEllipse(cx - pr, cy - pr, pr * 2.0f, pr * 2.0f);
    }

    float bx = sx(proc_.ballNX.load(std::memory_order_relaxed) * board_.width);
    float by = sy(proc_.ballNY.load(std::memory_order_relaxed) * board_.topY);
    float br = (board_.ballRadius / board_.width) * area.getWidth();
    g.setColour(juce::Colours::white);
    g.fillEllipse(bx - br, by - br, br * 2.0f, br * 2.0f);

    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.setFont(12.0f);
    g.drawText("click: add   drag: move   right-click: delete   double-click: delay/reverb",
               area.reduced(6.0f).removeFromTop(16.0f), juce::Justification::centredLeft);
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
    board_.pegRest[n] = board_.restitution;
    board_.pegType[n] = 0;   // new pegs default to delay
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

void BoardComponent::commit() { proc_.commitBoard(board_); repaint(); }

void BoardComponent::mouseDown(const juce::MouseEvent& e) {
    float bx = toBoardX(e.position.x), by = toBoardY(e.position.y);
    int hit = pegAt(bx, by);

    if (e.mods.isRightButtonDown()) {           // delete
        if (hit >= 0) { removePeg(hit); commit(); }
        return;
    }
    if (hit >= 0) {                              // grab to move
        dragIdx_ = hit;
    } else if (addPeg(bx, by)) {                 // add, then drag to position
        dragIdx_ = board_.pegCount - 1;
        repaint();
    }
}

void BoardComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragIdx_ < 0) return;
    float bx = juce::jlimit(0.05f, board_.width - 0.05f, toBoardX(e.position.x));
    float by = juce::jlimit(board_.exitY + 0.05f, board_.topY - 0.02f, toBoardY(e.position.y));
    board_.pegX[dragIdx_] = bx;
    board_.pegY[dragIdx_] = by;
    repaint();   // visual follows live; commit (physics re-init) on mouse-up
}

void BoardComponent::mouseUp(const juce::MouseEvent&) {
    if (dragIdx_ >= 0) { dragIdx_ = -1; commit(); }
}

void BoardComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    int hit = pegAt(toBoardX(e.position.x), toBoardY(e.position.y));
    if (hit >= 0) { board_.pegType[hit] = 1 - board_.pegType[hit]; commit(); }
}
