#include "BoardComponent.h"
#include "PluginProcessor.h"
#include <algorithm>

using EditType = PlinkoAudioProcessor::EditType;

namespace {
    enum MenuId {
        miDelay = 1, miReverb, miFlip,
        miGrow, miShrink, miBounceUp, miBounceDn,
        miMirror, miDuplicate, miApplyBrush,
        miAlignRow, miAlignCol, miDistH, miDistV,
        miDelete
    };
    constexpr float kDragSlop = 4.0f;   // px: below this a press+release is a click, not a drag
}

BoardComponent::BoardComponent(PlinkoAudioProcessor& p) : proc_(p) {
    board_ = proc_.board();   // start from the processor's current board
    setWantsKeyboardFocus(true);
    startTimerHz(60);
}
BoardComponent::~BoardComponent() { stopTimer(); }

void BoardComponent::timerCallback() {
    board_.width = proc_.boardW.load(std::memory_order_relaxed);  // track the live board width
    repaint();
}

bool BoardComponent::isSelected(int i) const {
    return std::find(sel_.begin(), sel_.end(), i) != sel_.end();
}

void BoardComponent::paint(juce::Graphics& g) {
    auto area = boardRect();
    g.fillAll(juce::Colour(0xff15151c));
    g.setColour(juce::Colour(0xff202028));
    g.fillRoundedRectangle(area, 6.0f);

    g.setColour(juce::Colours::dimgrey);
    g.drawLine(sx(xMin()), sy(0.0f), sx(xMin()), sy(board_.topY), 2.0f);
    g.drawLine(sx(xMax()), sy(0.0f), sx(xMax()), sy(board_.topY), 2.0f);

    {   // clip ONLY the pegs to the board (overhang hidden). The ball is NEVER clipped.
        juce::Graphics::ScopedSaveState clipState(g);
        g.reduceClipRegion(area.toNearestInt());
        for (int i = 0; i < board_.pegCount; ++i) {
            float pr = board_.pegRad[i] * scale();   // per-peg size
            float cx = sx(board_.pegX[i]), cy = sy(board_.pegY[i]);
            if (isSelected(i)) {                      // selection halo
                g.setColour(juce::Colours::white);
                g.drawEllipse(cx - pr - 3.0f, cy - pr - 3.0f, (pr + 3.0f) * 2.0f, (pr + 3.0f) * 2.0f, 2.0f);
            }
            juce::Colour c = (board_.pegType[i] == 1) ? juce::Colour(0xff5bc0be)   // reverb = teal
                                                      : juce::Colour(0xffe0a458);  // delay  = amber
            g.setColour(c);
            g.fillEllipse(cx - pr, cy - pr, pr * 2.0f, pr * 2.0f);
        }
    }

    float bx = sx(xMin() + proc_.ballNX.load(std::memory_order_relaxed) * board_.width);
    float by = sy(proc_.ballNY.load(std::memory_order_relaxed) * board_.topY);
    float br = proc_.ballR.load(std::memory_order_relaxed) * scale();   // live ball size
    g.setColour(juce::Colours::white);
    g.fillEllipse(bx - br, by - br, br * 2.0f, br * 2.0f);

    if (!proc_.running_.load()) {   // stopped: highlight the ball as the draggable start point
        g.setColour(juce::Colour(0xff5bc0be));
        g.drawEllipse(bx - br * 1.6f, by - br * 1.6f, br * 3.2f, br * 3.2f, 2.0f);
    }

    if (marquee_) {                 // rubber-band selection rectangle
        auto r = juce::Rectangle<float>(marqA_, marqB_);
        g.setColour(juce::Colour(0xff5bc0be).withAlpha(0.12f)); g.fillRect(r);
        g.setColour(juce::Colours::white.withAlpha(0.8f));      g.drawRect(r, 1.0f);
    }

    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.setFont(12.0f);
    const char* hint = proc_.running_.load()
        ? "click: add   drag empty: select   shift-click: +select   right-click selection: menu   Ctrl+Z: undo"
        : "stopped - drag the ball to set the start point";
    g.drawText(hint, area.reduced(6.0f).removeFromTop(16.0f), juce::Justification::centredLeft);
}

int BoardComponent::pegAt(float bx, float by) const {
    int best = -1; float bestD2 = 1e9f;
    for (int i = 0; i < board_.pegCount; ++i) {
        float dx = bx - board_.pegX[i], dy = by - board_.pegY[i];
        float d2 = dx * dx + dy * dy;
        float hitR = board_.pegRad[i] * 1.15f;   // this peg's own size (+ a little grab forgiveness)
        if (d2 <= hitR * hitR && d2 < bestD2) { bestD2 = d2; best = i; }  // nearest peg under the cursor
    }
    return best;
}

bool BoardComponent::addPeg(float bx, float by) {
    if (board_.pegCount >= 128) return false;
    // allow centers right up to the edges so a peg can clip ~half off the board
    if (bx < xMin() || bx > xMax() || by < board_.exitY || by > board_.topY)
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
    board_.pegRad[i] = board_.pegRad[last];
    board_.pegType[i] = board_.pegType[last];
    board_.pegCount = last;
}

void BoardComponent::eraseAt(int i) {
    PlinkoAudioProcessor::Edit ed;
    ed.type = EditType::Delete;
    ed.idx = i;
    proc_.pushEdit(ed);
    removePeg(i);   // mirror locally (same swap-remove the physics will do)
    repaint();
}

void BoardComponent::clearAllPegs() {
    pushUndo();
    board_.pegCount = 0;
    clearSelection();
    PlinkoAudioProcessor::Edit ed;
    ed.type = EditType::Clear;
    proc_.pushEdit(ed);
    repaint();
}

void BoardComponent::revertToDefault() {
    pushUndo();
    board_ = PlinkoAudioProcessor::defaultBoard();
    clearSelection();
    PlinkoAudioProcessor::Edit ed;
    ed.type = EditType::Reset;
    proc_.pushEdit(ed);
    repaint();
}

void BoardComponent::pushBulk() {
    PlinkoAudioProcessor::Edit ed;
    ed.type = EditType::BulkSet;
    ed.snapshot = std::make_shared<BoardParams>(board_);
    proc_.pushEdit(ed);
}

void BoardComponent::pushUndo() {
    undo_.push_back(board_);
    if (undo_.size() > 64) undo_.erase(undo_.begin());
}

void BoardComponent::undoLast() {
    if (undo_.empty()) return;
    board_ = undo_.back();
    undo_.pop_back();
    clearSelection();
    pushBulk();        // rebuild physics from the restored board (ball keeps flowing)
    repaint();
}

void BoardComponent::selectInMarquee(juce::Point<float> a, juce::Point<float> b, bool add) {
    if (!add) clearSelection();
    auto r = juce::Rectangle<float>(a, b);
    for (int i = 0; i < board_.pegCount; ++i) {
        juce::Point<float> px(sx(board_.pegX[i]), sy(board_.pegY[i]));
        if (r.contains(px) && !isSelected(i)) sel_.push_back(i);
    }
}

void BoardComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    downPos_ = e.position;
    gestureMoved_ = false;
    float bx = toBoardX(e.position.x), by = toBoardY(e.position.y);

    if (!proc_.running_.load() && !e.mods.isRightButtonDown()) {  // grab the start point
        float dx = bx - board_.dropX, dy = by - board_.dropY, rr = proc_.ballR.load() * 2.0f;
        if (dx * dx + dy * dy < rr * rr) { dragDrop_ = true; return; }
    }

    int hit = pegAt(bx, by);

    if (e.mods.isRightButtonDown()) {            // right: menu (in selection) or erase
        if (hit >= 0 && isSelected(hit)) { showContextMenu(e); return; }
        preState_ = board_;                      // group a sweep-erase into one undo
        if (hit >= 0) { eraseAt(hit); gestureMoved_ = true; }
        return;
    }

    if (e.mods.isShiftDown()) {                  // shift-click toggles selection
        if (hit >= 0) {
            auto it = std::find(sel_.begin(), sel_.end(), hit);
            if (it != sel_.end()) sel_.erase(it); else sel_.push_back(hit);
            repaint();
        }
        return;
    }

    if (hit >= 0) {                              // grab to move
        preState_ = board_;
        if (isSelected(hit) && sel_.size() > 1) { draggingSel_ = true; lastDrag_ = e.position; }
        else { clearSelection(); dragIdx_ = hit; repaint(); }
        return;
    }

    // empty space: defer to mouse-up -> click adds a peg, drag becomes a marquee
    preState_ = board_;
    marquee_ = true;
    marqA_ = marqB_ = e.position;
}

void BoardComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragDrop_) {                             // move the start point
        float bx = juce::jlimit(xMin() + 0.02f, xMax() - 0.02f, toBoardX(e.position.x));
        float by = juce::jlimit(board_.exitY + 0.1f, board_.topY - 0.02f, toBoardY(e.position.y));
        board_.dropX = bx; board_.dropY = by;
        PlinkoAudioProcessor::Edit ed;
        ed.type = EditType::SetDrop;
        ed.x = bx; ed.y = by;
        proc_.pushEdit(ed);
        repaint();
        return;
    }
    if (e.mods.isRightButtonDown()) {            // sweep-erase
        int hit = pegAt(toBoardX(e.position.x), toBoardY(e.position.y));
        if (hit >= 0) { eraseAt(hit); gestureMoved_ = true; }
        return;
    }
    if (marquee_) { marqB_ = e.position; repaint(); return; }

    if (draggingSel_) {                          // move the whole selection together
        float dx = toBoardX(e.position.x) - toBoardX(lastDrag_.x);
        float dy = toBoardY(e.position.y) - toBoardY(lastDrag_.y);
        lastDrag_ = e.position;
        for (int i : sel_) {
            board_.pegX[i] = juce::jlimit(xMin(), xMax(), board_.pegX[i] + dx);
            board_.pegY[i] = juce::jlimit(board_.exitY, board_.topY, board_.pegY[i] + dy);
        }
        gestureMoved_ = true;
        repaint();
        return;
    }

    if (dragIdx_ >= 0) {
        board_.pegX[dragIdx_] = juce::jlimit(xMin(), xMax(), toBoardX(e.position.x));
        board_.pegY[dragIdx_] = juce::jlimit(board_.exitY, board_.topY, toBoardY(e.position.y));
        gestureMoved_ = true;
        repaint();   // physics catches up on mouse-up (one Move edit, no ball reset)
    }
}

void BoardComponent::mouseUp(const juce::MouseEvent& e) {
    if (dragDrop_) { dragDrop_ = false; return; }

    if (marquee_) {
        marquee_ = false;
        if (downPos_.getDistanceFrom(e.position) < kDragSlop) {   // a click -> add a peg
            clearSelection();
            if (addPeg(toBoardX(downPos_.x), toBoardY(downPos_.y))) {
                int idx = board_.pegCount - 1;
                undo_.push_back(preState_); if (undo_.size() > 64) undo_.erase(undo_.begin());
                PlinkoAudioProcessor::Edit ed;
                ed.type = EditType::Add;
                ed.x = board_.pegX[idx]; ed.y = board_.pegY[idx];
                ed.rest = board_.pegRest[idx]; ed.pegType = board_.pegType[idx]; ed.radius = board_.pegRad[idx];
                proc_.pushEdit(ed);
            }
        } else {                                                  // a drag -> marquee select
            selectInMarquee(marqA_, marqB_, e.mods.isShiftDown());
        }
        repaint();
        return;
    }

    if (draggingSel_) {
        draggingSel_ = false;
        if (gestureMoved_) { undo_.push_back(preState_); if (undo_.size() > 64) undo_.erase(undo_.begin()); pushBulk(); }
        return;
    }

    if (dragIdx_ >= 0) {
        int idx = dragIdx_; dragIdx_ = -1;
        if (gestureMoved_) {
            undo_.push_back(preState_); if (undo_.size() > 64) undo_.erase(undo_.begin());
            PlinkoAudioProcessor::Edit ed;
            ed.type = EditType::Move;
            ed.idx = idx; ed.x = board_.pegX[idx]; ed.y = board_.pegY[idx];
            proc_.pushEdit(ed);
        }
        return;
    }

    if (gestureMoved_) {   // a right-click erase / sweep just finished -> one undo entry
        undo_.push_back(preState_); if (undo_.size() > 64) undo_.erase(undo_.begin());
        gestureMoved_ = false;
    }
}

void BoardComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    int hit = pegAt(toBoardX(e.position.x), toBoardY(e.position.y));
    if (hit < 0) return;
    pushUndo();
    board_.pegType[hit] = 1 - board_.pegType[hit];
    PlinkoAudioProcessor::Edit ed;
    ed.type = EditType::SetType;
    ed.idx = hit; ed.pegType = board_.pegType[hit];
    proc_.pushEdit(ed);
    repaint();
}

void BoardComponent::showContextMenu(const juce::MouseEvent& e) {
    const int n = (int)sel_.size();
    auto cnt = [n] { return " (" + juce::String(n) + ")"; };

    juce::PopupMenu typeM;
    typeM.addItem(miDelay,  "Delay");
    typeM.addItem(miReverb, "Reverb");
    typeM.addItem(miFlip,   "Flip");

    juce::PopupMenu alignM;
    alignM.addItem(miAlignRow, "Align to row");
    alignM.addItem(miAlignCol, "Align to column");
    alignM.addItem(miDistH,    "Distribute horizontally");
    alignM.addItem(miDistV,    "Distribute vertically");

    juce::PopupMenu m;
    m.addSectionHeader(juce::String(n) + (n == 1 ? " peg selected" : " pegs selected"));
    m.addSubMenu("Change type", typeM);
    m.addItem(miGrow,     "Grow" + cnt());
    m.addItem(miShrink,   "Shrink" + cnt());
    m.addItem(miBounceUp, "Bounce +" + cnt());
    m.addItem(miBounceDn, "Bounce -" + cnt());
    m.addSeparator();
    m.addItem(miMirror,    "Mirror horizontal");
    m.addItem(miDuplicate, "Duplicate" + cnt());
    m.addItem(miApplyBrush, "Apply " + juce::String(brushType_ == 0 ? "Delay" : "Reverb") + " brush");
    m.addSubMenu("Align & distribute", alignM);
    m.addSeparator();
    m.addItem(miDelete, "Delete" + cnt());

    auto self = juce::Component::SafePointer<BoardComponent>(this);
    auto pos = e.getScreenPosition();
    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetScreenArea(juce::Rectangle<int>(pos.x, pos.y, 1, 1)),
                    [self](int id) { if (self != nullptr && id != 0) self->runMenuOp(id); });
}

void BoardComponent::runMenuOp(int id) {
    if (sel_.empty()) return;
    pushUndo();

    auto distribute = [this](bool horizontal) {
        std::vector<int> s = sel_;
        if (s.size() < 3) return;   // 2 pegs are already the extremes; nothing to space
        std::sort(s.begin(), s.end(), [this, horizontal](int a, int b) {
            return horizontal ? board_.pegX[a] < board_.pegX[b] : board_.pegY[a] < board_.pegY[b];
        });
        float lo = horizontal ? board_.pegX[s.front()] : board_.pegY[s.front()];
        float hi = horizontal ? board_.pegX[s.back()]  : board_.pegY[s.back()];
        int cnt = (int)s.size();
        for (int k = 0; k < cnt; ++k) {
            float t = lo + (hi - lo) * (float)k / (float)(cnt - 1);
            if (horizontal) board_.pegX[s[k]] = t; else board_.pegY[s[k]] = t;
        }
    };

    switch (id) {
        case miDelay:    for (int i : sel_) board_.pegType[i] = 0; break;
        case miReverb:   for (int i : sel_) board_.pegType[i] = 1; break;
        case miFlip:     for (int i : sel_) board_.pegType[i] = 1 - board_.pegType[i]; break;
        case miGrow:     for (int i : sel_) board_.pegRad[i]  = juce::jmin(0.06f, board_.pegRad[i] * 1.25f); break;
        case miShrink:   for (int i : sel_) board_.pegRad[i]  = juce::jmax(0.005f, board_.pegRad[i] / 1.25f); break;
        case miBounceUp: for (int i : sel_) board_.pegRest[i] = juce::jmin(2.0f, board_.pegRest[i] + 0.2f); break;
        case miBounceDn: for (int i : sel_) board_.pegRest[i] = juce::jmax(0.0f, board_.pegRest[i] - 0.2f); break;
        case miMirror:   for (int i : sel_) board_.pegX[i]    = 2.0f * kBoardCenterX - board_.pegX[i]; break;  // reflect across the board center
        case miApplyBrush:
            for (int i : sel_) {
                board_.pegType[i] = brushType_;
                board_.pegRest[i] = brushBounce_[brushType_];
                board_.pegRad[i]  = brushSize_[brushType_];
            }
            break;
        case miAlignRow: { float s = 0; for (int i : sel_) s += board_.pegY[i]; float a = s / sel_.size(); for (int i : sel_) board_.pegY[i] = a; } break;
        case miAlignCol: { float s = 0; for (int i : sel_) s += board_.pegX[i]; float a = s / sel_.size(); for (int i : sel_) board_.pegX[i] = a; } break;
        case miDistH:    distribute(true);  break;
        case miDistV:    distribute(false); break;
        case miDuplicate: {
            std::vector<int> src = sel_, newSel;
            const float off = 0.05f;
            for (int i : src) {
                if (board_.pegCount >= 128) break;
                int n = board_.pegCount;
                board_.pegX[n]    = juce::jlimit(xMin(), xMax(), board_.pegX[i] + off);
                board_.pegY[n]    = juce::jlimit(board_.exitY, board_.topY, board_.pegY[i] - off);
                board_.pegRest[n] = board_.pegRest[i];
                board_.pegRad[n]  = board_.pegRad[i];
                board_.pegType[n] = board_.pegType[i];
                board_.pegCount   = n + 1;
                newSel.push_back(n);
            }
            sel_ = newSel;   // select the copies so they can be dragged into place
        } break;
        case miDelete: {
            bool keep[128];
            for (int i = 0; i < board_.pegCount; ++i) keep[i] = true;
            for (int i : sel_) if (i >= 0 && i < board_.pegCount) keep[i] = false;
            BoardParams nb = board_;   // carries non-peg fields (width, drop, etc.)
            int w = 0;
            for (int i = 0; i < board_.pegCount; ++i) if (keep[i]) {
                nb.pegX[w] = board_.pegX[i]; nb.pegY[w] = board_.pegY[i];
                nb.pegRest[w] = board_.pegRest[i]; nb.pegRad[w] = board_.pegRad[i]; nb.pegType[w] = board_.pegType[i];
                ++w;
            }
            nb.pegCount = w;
            board_ = nb;
            clearSelection();
        } break;
    }

    pushBulk();
    repaint();
}

void BoardComponent::nudgeSelection(float dx, float dy) {
    if (sel_.empty()) return;
    pushUndo();
    for (int i : sel_) {
        board_.pegX[i] = juce::jlimit(xMin(), xMax(), board_.pegX[i] + dx);
        board_.pegY[i] = juce::jlimit(board_.exitY, board_.topY, board_.pegY[i] + dy);
    }
    pushBulk();
    repaint();
}

bool BoardComponent::keyPressed(const juce::KeyPress& k) {
    const bool cmd = k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown();
    const int  c   = k.getKeyCode();

    if (c == juce::KeyPress::deleteKey || c == juce::KeyPress::backspaceKey) {
        runMenuOp(miDelete);   // shares the bulk-delete + undo path
        return true;
    }
    if (cmd && (c == 'Z' || c == 'z')) { undoLast(); return true; }
    if (cmd && (c == 'D' || c == 'd')) { runMenuOp(miDuplicate); return true; }
    if (c == juce::KeyPress::escapeKey) { clearSelection(); repaint(); return true; }

    const float step = 0.01f;
    if (c == juce::KeyPress::leftKey)  { nudgeSelection(-step, 0.0f); return true; }
    if (c == juce::KeyPress::rightKey) { nudgeSelection( step, 0.0f); return true; }
    if (c == juce::KeyPress::upKey)    { nudgeSelection(0.0f,  step); return true; }
    if (c == juce::KeyPress::downKey)  { nudgeSelection(0.0f, -step); return true; }
    return false;
}
