#pragma once

#include "ReaderWriter.h"
#include "Auxilaries.h"

#include <SFML/Graphics.hpp>
#include <optional>
#include <memory>
#include <string>

namespace Nardi {

class SFMLRW : public ReaderWriter
{
public:
    SFMLRW(Game& game, Controller& c,
           unsigned width = 1100, unsigned height = 720);

    // Core loop hooks
    virtual status_codes PollInput() override;
    virtual void Render() const override;

    virtual void InstructionMessage(std::string m) const override;
    virtual void ErrorMessage(std::string m) const override;

    virtual void OnGameEvent(const GameEvent& e) override;

private:
    // Window/resources
    mutable sf::RenderWindow window;
    mutable sf::Font font;
    mutable bool fontLoaded = false;

    // Board image resources (SFML 3: sf::Sprite has no default ctor)
    mutable sf::Texture boardTexture;
    mutable bool boardTextureLoaded = false;
    mutable std::optional<sf::Sprite> boardSprite;

    bool game_over_screen = false;

    // Layout
    unsigned W, H;
    float margin = 40.f;
    float hudH   = 90.f;
    float diceW  = 220.f;

    // UI state (for click source / click die UX)
    mutable std::string statusLine = "Click a source point, then click a die.";

    // ---- Board image layout tuning (percentages of boardRect) ----
    // These define the "playable" regions INSIDE the thick wooden border and the center divider.
    // Adjust if you swap board art later.
    float borderPadXFrac   = 0.140f; // left/right inner padding from boardRect
    float borderPadYFrac   = 0.050f; // top/bottom inner padding from boardRect
    float centerGapFrac    = 0.07f; // width of the center divider gap inside boardRect

    // ---- Geometry helpers ----
    sf::FloatRect boardRect() const;
    sf::FloatRect boardImageRect() const;
    sf::FloatRect dicePanelRect() const;
    sf::FloatRect dieRect(bool idx) const; // idx=false left die, true right die

    // Inner play areas inside the board image (left and right halves)
    sf::FloatRect boardInnerLeftRect() const;
    sf::FloatRect boardInnerRightRect() const;

    // Cell mapping (unchanged logic; only rectangles are now within the inner play areas)
    sf::FloatRect cellRect(int row, int col) const;

    std::optional<Coord> hitTestCell(sf::Vector2f p) const;
    std::optional<bool> hitTestDie(sf::Vector2f p) const;

    // ---- Drawing helpers ----
    void drawBoardGrid() const;     // now draws board image + selection overlays (not a colored grid)
    void drawPieces() const;
    void drawDice() const;
    void drawHUD() const;

    void drawGameOverOverlay() const;

    void drawText(const std::string& s, float x, float y, unsigned size = 18) const;

    // board access
    int cellVal(int row, int col) const;

    // font loading
    void tryLoadFont();

    // board image loading
    void tryLoadBoardImage() const;
};

struct SFMLRWFactory : public IRWFactory{
    virtual std::unique_ptr<ReaderWriter> make(Game& g, Controller& c) const override;
};

inline
std::unique_ptr<ReaderWriter> SFMLRWFactory::make(Game& g, Controller& c) const
{
    return std::make_unique<SFMLRW>(g, c);
}

} // namespace Nardi
