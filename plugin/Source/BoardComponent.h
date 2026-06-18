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

    juce::Rectangle<float> boardArea() const { return getLocalBounds().toFloat().reduced(8.0f); }
    float sx(float x) const { auto a = boardArea(); return a.getX() + (x / board_.width) * a.getWidth(); }
    float sy(float y) const { auto a = boardArea(); return a.getY() + (1.0f - y / board_.topY) * a.getHeight(); }
    float toBoardX(float px) const { auto a = boardArea(); return (px - a.getX()) / a.getWidth() * board_.width; }
    float toBoardY(float py) const { auto a = boardArea(); return (1.0f - (py - a.getY()) / a.getHeight()) * board_.topY; }

    int  pegAt(float bx, float by) const;     // index of peg near (bx,by), or -1
    bool addPeg(float bx, float by);          // returns false if full / out of bounds
    void removePeg(int i);
    void commit();                            // push board_ to the processor

    PlinkoAudioProcessor& proc_;
    BoardParams board_;     // editable working copy (authoritative for drawing/editing)
    int dragIdx_ = -1;
};
