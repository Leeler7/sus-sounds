#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PhysicsCore.h"

class PlinkoAudioProcessor;

// Interactive Plinko board: draws pegs (color-coded by type, bumpers highlighted) + the
// live ball, and lets the user edit the board:
//   left-click empty   -> add a peg (then drag to position)
//   left-drag a peg     -> move it
//   right-click a peg   -> delete it
//   double-click a peg  -> toggle delay <-> reverb
// Edits commit to the processor on mouse-up (which re-inits the live physics).
class BoardComponent : public juce::Component, private juce::Timer {
public:
    explicit BoardComponent(PlinkoAudioProcessor& p);
    ~BoardComponent() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }

    // Aspect-correct board rect: one uniform scale for X and Y so circles stay circles and
    // the drawing matches the physics exactly (no distortion).
    juce::Rectangle<float> boardRect() const {
        auto a = getLocalBounds().toFloat().reduced(8.0f);
        float s = juce::jmin(a.getWidth() / board_.width, a.getHeight() / board_.topY);
        float w = board_.width * s, h = board_.topY * s;
        return { a.getCentreX() - w * 0.5f, a.getCentreY() - h * 0.5f, w, h };
    }
    float scale() const { return boardRect().getWidth() / board_.width; }
    float sx(float x) const { return boardRect().getX() + x * scale(); }
    float sy(float y) const { return boardRect().getBottom() - y * scale(); }   // y=0 at bottom
    float toBoardX(float px) const { return (px - boardRect().getX()) / scale(); }
    float toBoardY(float py) const { return (boardRect().getBottom() - py) / scale(); }

    int  pegAt(float bx, float by) const;     // index of peg near (bx,by), or -1
    bool addPeg(float bx, float by);          // returns false if full / out of bounds (local only)
    void removePeg(int i);                    // local only
    void eraseAt(int i);                      // delete peg i locally + enqueue the edit

    PlinkoAudioProcessor& proc_;
    BoardParams board_;     // editable working copy (authoritative for drawing/editing)
    int dragIdx_ = -1;
    bool dragDrop_ = false; // dragging the start point (only while stopped)
};
