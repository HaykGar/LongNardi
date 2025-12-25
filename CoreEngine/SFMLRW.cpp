#include "SFMLRW.h"

#include <cmath>
#include <iostream>
#include <optional>

namespace Nardi {

SFMLRW::SFMLRW(Game& game, Controller& c, unsigned width, unsigned height)
    : ReaderWriter(game, c),
      window(sf::VideoMode({width, height}), "Nardi (SFML)", sf::Style::Titlebar | sf::Style::Close),
      W(width), H(height)
{
    window.setVerticalSyncEnabled(true);
    tryLoadFont();
}

void SFMLRW::tryLoadFont()
{
    const char* candidates[] = {
        "assets/Roboto-Regular.ttf",
        "assets/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Verdana.ttf",
        "/System/Library/Fonts/Supplemental/Tahoma.ttf",
        "C:/Windows/Fonts/Arial.ttf" // Added common Windows path
    };

    for (const char* p : candidates)
    {
        // FIX 1: loadFromFile -> openFromFile in SFML 3
        if (font.openFromFile(p))
        {
            fontLoaded = true;
            return;
        }
    }
    fontLoaded = false;
    std::cerr << "Warning: No font loaded. Text will not display.\n";
}

sf::FloatRect SFMLRW::boardRect() const
{
    float x = margin;
    float y = margin + hudH;
    float w = (float)W - 2.f * margin - diceW;
    float h = (float)H - (margin + hudH) - margin;
    return sf::FloatRect({x, y}, {w, h});
}

sf::FloatRect SFMLRW::dicePanelRect() const
{
    auto br = boardRect();
    float x = br.position.x + br.size.x + 20.f;
    float y = br.position.y;
    float w = diceW - 20.f;
    float h = br.size.y;
    return sf::FloatRect({x, y}, {w, h});
}

sf::FloatRect SFMLRW::cellRect(int row, int col) const
{
    auto br = boardRect();
    float cellW = br.size.x / (float)COLS;
    float cellH = br.size.y / (float)ROWS;

    // Reverse top row visually
    int visualCol = col;
    if (row == 0)
        visualCol = COLS - 1 - col;

    return sf::FloatRect(
        { br.position.x + visualCol * cellW,
          br.position.y + row * cellH },
        { cellW, cellH }
    );
}


sf::FloatRect SFMLRW::dieRect(bool idx) const
{
    auto dr = dicePanelRect();
    float pad = 16.f;
    float size = std::min((dr.size.x - 3.f * pad) / 2.f, 90.f);
    float y = dr.position.y + pad;
    float x0 = dr.position.x + pad;
    float x1 = x0 + size + pad;
    float x = idx ? x1 : x0;
    return sf::FloatRect({x, y}, {size, size});
}

int SFMLRW::cellVal(int row, int col) const
{
    const auto& b = g.GetBoardRef();
    return (int)b.at((size_t)row, (size_t)col);
}

std::optional<Coord> SFMLRW::hitTestCell(sf::Vector2f p) const
{
    auto br = boardRect();
    if (!br.contains(p))
        return std::nullopt;

    float cellW = br.size.x / (float)COLS;
    float cellH = br.size.y / (float)ROWS;

    int visualCol = (int)((p.x - br.position.x) / cellW);
    int row       = (int)((p.y - br.position.y) / cellH);

    if (row < 0 || row >= ROWS || visualCol < 0 || visualCol >= COLS)
        return std::nullopt;

    // Undo visual flip for top row
    int logicalCol = visualCol;
    if (row == 0)
        logicalCol = COLS - 1 - visualCol;

    return Coord(row, logicalCol);
}


std::optional<bool> SFMLRW::hitTestDie(sf::Vector2f p) const
{
    auto r0 = dieRect(false);
    auto r1 = dieRect(true);
    if (r0.contains(p)) return false;
    if (r1.contains(p)) return true;
    return std::nullopt;
}

void SFMLRW::drawText(const std::string& s, float x, float y, unsigned size) const
{
    if (!fontLoaded) return;
    
    // FIX 2: sf::Text requires font in constructor now
    sf::Text t(font); 
    t.setString(s);
    t.setCharacterSize(size);
    t.setPosition({x, y});
    t.setFillColor(sf::Color::White);
    window.draw(t);
}

void SFMLRW::drawBoardGrid() const
{
    auto br = boardRect();

    sf::RectangleShape bg(br.size);
    bg.setPosition(br.position);
    bg.setFillColor(sf::Color(35, 70, 45));
    window.draw(bg);

    for (int r = 0; r < ROWS; ++r)
    {
        for (int c = 0; c < COLS; ++c)
        {
            auto cr = cellRect(r, c);
            sf::RectangleShape cell({cr.size.x - 2.f, cr.size.y - 2.f});
            cell.setPosition({cr.position.x + 1.f, cr.position.y + 1.f});

            bool alt = ((r + c) % 2) == 0;
            cell.setFillColor(alt ? sf::Color(60, 95, 70) : sf::Color(50, 85, 62));

            if (hasSelection && selected.row == r && selected.col == c)
            {
                cell.setFillColor(sf::Color(90, 120, 90));
            }

            window.draw(cell);
        }
    }

    sf::RectangleShape mid({br.size.x, 3.f});
    mid.setPosition({br.position.x, br.position.y + br.size.y / 2.f - 1.5f});
    mid.setFillColor(sf::Color(20, 40, 25));
    window.draw(mid);
}

void SFMLRW::drawPieces() const
{
    const auto& b = g.GetBoardRef();
    bool topRowIsCurrentPlayersStart = b.PlayerIdx();

    for (int r = 0; r < ROWS; ++r)
    {
        for (int c = 0; c < COLS; ++c)
        {
            int raw = cellVal(r, c);
            int count = std::abs(raw);
            if (count == 0) continue;

            int ownerSign = (raw > 0) ? 1 : -1;

            auto cr = cellRect(r, c);
            float radius = std::min(cr.size.x, cr.size.y) * 0.18f;

            bool isTop = (r == 0);
            float baseX = cr.position.x + cr.size.x / 2.f;
            float baseY = isTop ? (cr.position.y + radius + 10.f) : (cr.position.y + cr.size.y - radius - 10.f);

            int maxStack = std::min(count, 5);

            for (int i = 0; i < maxStack; ++i)
            {
                float dy = (radius * 2.1f) * i;
                float y = isTop ? (baseY + dy) : (baseY - dy);

                sf::CircleShape piece(radius);
                piece.setOrigin({radius, radius});
                piece.setPosition({baseX, y});

                if (ownerSign > 0)
                    piece.setFillColor(sf::Color(220, 220, 220));
                else
                    piece.setFillColor(sf::Color(40, 40, 40));

                piece.setOutlineThickness(2.f);
                piece.setOutlineColor(sf::Color(10, 10, 10));

                window.draw(piece);

                if (i == 0 && count > 5 && fontLoaded)
                {
                    std::string txt = std::to_string(count);
                    // FIX 2: Pass font to constructor
                    sf::Text t(font);
                    t.setString(txt);
                    t.setCharacterSize(16);
                    t.setFillColor(ownerSign > 0 ? sf::Color::Black : sf::Color::White);

                    auto bounds = t.getLocalBounds();
                    t.setOrigin({bounds.position.x + bounds.size.x / 2.f,
                                 bounds.position.y + bounds.size.y / 2.f});
                    t.setPosition({baseX, y});

                    window.draw(t);
                }
            }
        }
    }
}

void SFMLRW::drawDice() const
{
    auto panel = dicePanelRect();

    sf::RectangleShape pbg(panel.size);
    pbg.setPosition(panel.position);
    pbg.setFillColor(sf::Color(25, 25, 25));
    window.draw(pbg);

    for (int i = 0; i < 2; ++i)
    {
        auto r = dieRect(i);

        sf::RectangleShape die(r.size);
        die.setPosition(r.position);

        bool gray = !g.CanUseDice(i) || awaitingRoll;

        if (gray)
        {
            die.setFillColor(sf::Color(170, 170, 170));   // gray
            die.setOutlineColor(sf::Color(80, 80, 80));
        }
        else
        {
            die.setFillColor(sf::Color(230, 230, 230));   // normal
            die.setOutlineColor(sf::Color(110, 110, 110));
        }

        die.setOutlineThickness(3.f);

        window.draw(die);

        int val = g.GetDice(i);
        if (fontLoaded)
        {
            // FIX 2: Pass font to constructor
            sf::Text t(font);
            t.setString(std::to_string(val));
            t.setCharacterSize(40);
            t.setFillColor(sf::Color::Black);
            auto b = t.getLocalBounds();
            t.setOrigin({b.position.x + b.size.x / 2.f, b.position.y + b.size.y / 2.f});
            t.setPosition({r.position.x + r.size.x / 2.f, r.position.y + r.size.y / 2.f});
            window.draw(t);
        }
    }

    if (fontLoaded)
    {
        drawText("Dice (click one):", panel.position.x + 16.f, panel.position.y + 120.f, 18);
        drawText("Keys: R=roll, Q=quit", panel.position.x + 16.f, panel.position.y + 150.f, 16);
    }
}

void SFMLRW::drawHUD() const
{
    sf::RectangleShape hud({(float)W, hudH});
    hud.setPosition({0.f, 0.f});
    hud.setFillColor(sf::Color(18, 18, 18));
    window.draw(hud);

    if (fontLoaded)
    {
        drawText(statusLine, margin, 18.f, 18);
        drawText("Click source point, then click die.", margin, 42.f, 16);
    }
}

void SFMLRW::Render() const
{
    window.clear(sf::Color(15, 15, 15));
    drawHUD();
    drawBoardGrid();
    drawPieces();
    drawDice();
    window.display();
}

status_codes SFMLRW::PollInput()
{
    while (const auto event = window.pollEvent())
    {
        if (event->is<sf::Event::Closed>())
        {
            return ctrl.ReceiveCommand(Command(Actions::QUIT));
        }

        if (const auto* k = event->getIf<sf::Event::KeyPressed>())
        {
            if (k->code == sf::Keyboard::Key::Q)
                return ctrl.ReceiveCommand(Command(Actions::QUIT));

            if (k->code == sf::Keyboard::Key::R)
                return ctrl.ReceiveCommand(Command(Actions::ROLL_DICE));

            if (k->code == sf::Keyboard::Key::U)
                return ctrl.ReceiveCommand(Command(Actions::UNDO));
        }

        if (const auto* m = event->getIf<sf::Event::MouseButtonPressed>())
        {
            if (m->button != sf::Mouse::Button::Left)
                continue;

            sf::Vector2f p((float)m->position.x, (float)m->position.y);

            if (auto die = hitTestDie(p))
            {
                return ctrl.ReceiveCommand(Command(*die));
            }

            if (auto c = hitTestCell(p))
            {
                hasSelection = true;
                selected = *c;
                return ctrl.ReceiveCommand(Command(c->row, c->col));
            }
        }
    }

    Render();
    return status_codes::SUCCESS;
}

void SFMLRW::OnGameEvent(const GameEvent& e)
{
    switch (e.code)
    {
        InstructionMessage(std::to_string(static_cast<int>(e.code)));
        case EventCode::QUIT:
            statusLine = "Game ended by user.";
            Render();
            window.close();
            return;

        case EventCode::TURN_SWITCH:
            // New turn: dice not rolled yet
            awaitingRoll = true;
            hasSelection = false;
            selected = Coord(-1, -1);

            InstructionMessage("New turn. Roll the dice.");
            // Render();
            return;

        case EventCode::DICE_ROLL:
            // Dice are now active
            awaitingRoll = false;
            Render();
            return;

        case EventCode::MOVE:
        case EventCode::REMOVE:
            hasSelection = false;
            selected = Coord(-1, -1);
            Render();
            return;

        default:
            Render();
            return;
    }
}


void SFMLRW::InstructionMessage(std::string m) const
{
    statusLine = std::move(m);
    Render();
}

void SFMLRW::ErrorMessage(std::string m) const
{
    statusLine = "Error: " + std::move(m);
    Render();
}

std::unique_ptr<ReaderWriter> SFMLRWFactory(Game& g, Controller& c)
{
    return std::make_unique<SFMLRW>(g, c);
}

} // namespace Nardi