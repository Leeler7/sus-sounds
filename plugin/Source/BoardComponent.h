#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "PhysicsCore.h"
#include "SoundEngine.h"   // kNumBuses

class PlinkoAudioProcessor;

// Interactive Plinko board. Single-peg editing + multi-select bulk editing.
//   left-click empty    -> add a peg
//   left-drag empty      -> marquee (rubber-band) select
//   left-click a peg      -> move it (clears selection)
//   shift+click a peg     -> toggle it in/out of the selection
//   left-drag a selected peg -> move the whole selection together
//   right-click in selection -> bulk context menu; right-click/drag elsewhere -> erase
//   double-click a peg   -> toggle delay <-> reverb
//   keys: Del = delete selection, Ctrl+D = duplicate, Ctrl+Z = undo, Esc = deselect, arrows = nudge
class BoardComponent : public juce::Component, private juce::Timer {
public:
    explicit BoardComponent(PlinkoAudioProcessor& p);
    ~BoardComponent() override;

    void clearAllPegs();      // remove every peg (undoable)
    void revertToDefault();   // restore the baseline board

    // Brush: properties applied to NEWLY placed pegs (existing pegs untouched).
    void setBrushType(int t)              { brushType_ = t; }
    void setBrushBus(int b)               { brushBus_ = juce::jlimit(0, kNumBuses - 1, b); }  // bus for new pegs
    int  brushType() const                { return brushType_; }
    // Per-bus peg presets (single source of truth; the editor sliders read/write these). A bus
    // stores bounce + size per peg type, alongside its effect character (which lives in the engine).
    void  setBusBounce(int bus, int type, float v) { busBounce_[bus & (kNumBuses - 1)][type & 1] = v; }
    void  setBusSize  (int bus, int type, float v) { busSize_  [bus & (kNumBuses - 1)][type & 1] = v; }
    void  setBusSend  (int bus, int type, float v) { busSend_  [bus & (kNumBuses - 1)][type & 1] = v; }
    void  setBusLevel (int bus, int type, float v) { busLevel_ [bus & (kNumBuses - 1)][type & 1] = v; }
    void  setBusTone  (int bus, int type, float v) { busTone_  [bus & (kNumBuses - 1)][type & 1] = v; }
    float busBounce(int bus, int type) const { return busBounce_[bus & (kNumBuses - 1)][type & 1]; }
    float busSize  (int bus, int type) const { return busSize_  [bus & (kNumBuses - 1)][type & 1]; }
    float busSend  (int bus, int type) const { return busSend_  [bus & (kNumBuses - 1)][type & 1]; }
    float busLevel (int bus, int type) const { return busLevel_ [bus & (kNumBuses - 1)][type & 1]; }
    float busTone  (int bus, int type) const { return busTone_  [bus & (kNumBuses - 1)][type & 1]; }
    void  resetBusPresets();   // all buses back to default peg profile

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void timerCallback() override;

    // Aspect-correct board rect: one uniform scale for X and Y so circles stay circles and
    // the drawing matches the physics exactly (no distortion).
    juce::Rectangle<float> boardRect() const {
        auto a = getLocalBounds().toFloat().reduced(8.0f);
        float s = juce::jmin(a.getWidth() / board_.width, a.getHeight() / board_.topY);
        float w = board_.width * s, h = board_.topY * s;
        return { a.getCentreX() - w * 0.5f, a.getCentreY() - h * 0.5f, w, h };
    }
    // Board spans [xMin, xMax], centered on kBoardCenterX (expands/contracts symmetrically).
    float xMin() const { return kBoardCenterX - board_.width * 0.5f; }
    float xMax() const { return kBoardCenterX + board_.width * 0.5f; }
    float scale() const { return boardRect().getWidth() / board_.width; }
    float sx(float x) const { return boardRect().getX() + (x - xMin()) * scale(); }
    float sy(float y) const { return boardRect().getBottom() - y * scale(); }   // y=0 at bottom
    float toBoardX(float px) const { return xMin() + (px - boardRect().getX()) / scale(); }
    float toBoardY(float py) const { return (boardRect().getBottom() - py) / scale(); }

    int  pegAt(float bx, float by) const;     // index of peg near (bx,by), or -1
    bool addPeg(float bx, float by);          // returns false if full / out of bounds (local only)
    void removePeg(int i);                    // local only
    void eraseAt(int i);                       // delete peg i locally + enqueue the edit

    // Selection helpers
    bool isSelected(int i) const;
    void clearSelection() { sel_.clear(); }
    void selectInMarquee(juce::Point<float> a, juce::Point<float> b, bool add);

    // Bulk + undo plumbing
    void pushBulk();     // enqueue a full-board BulkSet edit from the working copy
    void pushUndo();     // snapshot board_ onto the undo stack (call BEFORE mutating)
    void undoLast();
    void showContextMenu(const juce::MouseEvent&);
    void runMenuOp(int id);
    void nudgeSelection(float dx, float dy);

    PlinkoAudioProcessor& proc_;
    BoardParams board_;     // editable working copy (authoritative for drawing/editing)

    std::vector<int> sel_;          // selected peg indices (into board_)
    int dragIdx_ = -1;              // single peg being moved
    bool draggingSel_ = false;      // moving the whole selection
    bool dragDrop_ = false;         // dragging the start point (only while stopped)
    bool marquee_ = false;          // rubber-band selecting
    juce::Point<float> downPos_, marqA_, marqB_, lastDrag_;
    bool gestureMoved_ = false;     // did this drag actually change anything?
    BoardParams preState_;          // board_ snapshot captured at gesture start (for undo)

    std::vector<BoardParams> undo_; // undo stack (capped)

    int brushType_ = 0;                       // 0 = delay, 1 = reverb (for new pegs)
    int brushBus_  = 0;                       // effect bus for newly placed pegs
    float busBounce_[kNumBuses][2];           // [bus][type] bounce preset for new pegs
    float busSize_  [kNumBuses][2];           // [bus][type] size preset
    float busSend_  [kNumBuses][2];           // [bus][type] wet send
    float busLevel_ [kNumBuses][2];           // [bus][type] gain trim
    float busTone_  [kNumBuses][2];           // [bus][type] brightness bias
};
