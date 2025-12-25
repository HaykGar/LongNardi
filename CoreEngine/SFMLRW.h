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
    virtual void Render() const override;     // we treat this as Render()


    virtual void InstructionMessage(std::string m) const override;
    virtual void ErrorMessage(std::string m) const override;

    virtual void OnGameEvent(const GameEvent& e) override;

private:
    // Window/resources
    mutable sf::RenderWindow window;
    mutable sf::Font font;
    mutable bool fontLoaded = false;

    // Layout
    unsigned W, H;
    float margin = 40.f;
    float hudH   = 90.f;
    float diceW  = 220.f;

    // UI state (for click source / click die UX)
    mutable bool hasSelection = false;
    mutable Coord selected{ -1, -1 };
    mutable std::string statusLine = "Click a source point, then click a die.";

    // ---- Geometry helpers ----
    sf::FloatRect boardRect() const;
    sf::FloatRect dicePanelRect() const;
    sf::FloatRect dieRect(bool idx) const; // idx=false left die, true right die
    sf::FloatRect cellRect(int row, int col) const;

    std::optional<Coord> hitTestCell(sf::Vector2f p) const;
    std::optional<bool> hitTestDie(sf::Vector2f p) const;

    // ---- Drawing helpers ----

    void drawBoardGrid() const;
    void drawPieces() const;
    void drawDice() const;
    void drawHUD() const;

    void drawText(const std::string& s, float x, float y, unsigned size = 18) const;

    // board access
    int cellVal(int row, int col) const;

    // font loading
    void tryLoadFont();
};

} // namespace Nardi
