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
    // Board image is loaded lazily in Render() (const) via tryLoadBoardImage().
}

void SFMLRW::tryLoadFont()
{
    const char* candidates[] = {
        "assets/Roboto-Regular.ttf",
        "assets/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Verdana.ttf",
        "/System/Library/Fonts/Supplemental/Tahoma.ttf",
        "C:/Windows/Fonts/Arial.ttf"
    };

    for (const char* p : candidates)
    {
        if (font.openFromFile(p))   // SFML 3
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

void SFMLRW::tryLoadBoardImage() const
{
    if (boardTextureLoaded || boardSprite.has_value())
        return;

    // Try common paths. User requested "./images/BoardImg"
    const char* candidates[] = {
        "../images/BoardImg.png",
        "../images/BoardImg.jpg",
        "../images/BoardImg.jpeg",
    };

    for (const char* p : candidates)
    {
        if (boardTexture.loadFromFile(p))
        {
            boardTextureLoaded = true;
            boardSprite.emplace(boardTexture); // SFML 3: sprite constructed with texture
            return;
        }
    }

    boardTextureLoaded = false;
    // Not fatal: we can still draw fallback background.
    std::cerr << "Warning: Could not load board image from ./images/BoardImg(.png/.jpg). "
                 "Falling back to flat background.\n";
}

sf::FloatRect SFMLRW::boardInnerLeftRect() const
{
    auto br = boardRect();

    float padX = br.size.x * borderPadXFrac;
    float padY = br.size.y * borderPadYFrac;
    float gapW = br.size.x * centerGapFrac;

    float innerW = br.size.x - 2.f * padX - gapW;
    float halfW  = innerW * 0.5f;

    float x = br.position.x + padX;
    float y = br.position.y + padY;
    float w = halfW;
    float h = br.size.y - 2.f * padY;

    return sf::FloatRect({x, y}, {w, h});
}

sf::FloatRect SFMLRW::boardInnerRightRect() const
{
    auto br = boardRect();

    float padX = br.size.x * borderPadXFrac;
    float padY = br.size.y * borderPadYFrac;
    float gapW = br.size.x * centerGapFrac;

    float innerW = br.size.x - 2.f * padX - gapW;
    float halfW  = innerW * 0.5f;

    float x = br.position.x + padX + halfW + gapW;
    float y = br.position.y + padY;
    float w = halfW;
    float h = br.size.y - 2.f * padY;

    return sf::FloatRect({x, y}, {w, h});
}

sf::FloatRect SFMLRW::boardImageRect() const
{
    auto br = boardRect();

    if (!boardTextureLoaded)
        return br;

    auto texSize = boardTexture.getSize();
    float texW = static_cast<float>(texSize.x);
    float texH = static_cast<float>(texSize.y);

    float scale = std::min(br.size.x / texW, br.size.y / texH);

    float drawW = texW * scale;
    float drawH = texH * scale;

    float x = br.position.x + (br.size.x - drawW) * 0.5f;
    float y = br.position.y + (br.size.y - drawH) * 0.5f;

    return sf::FloatRect({x, y}, {drawW, drawH});
}


sf::FloatRect SFMLRW::cellRect(int row, int col) const
{
    // IMPORTANT: same logical mapping as before (including top-row visual flip),
    // but rectangles are now computed inside the inner playable regions of the board image.

    // Reverse top row visually
    int visualCol = col;
    if (row == 0)
        visualCol = COLS - 1 - col;

    // Split into halves (0..5 left half, 6..11 right half) based on VISUAL column
    const bool rightHalf = (visualCol >= (COLS / 2));
    const int localCol   = rightHalf ? (visualCol - (COLS / 2)) : visualCol;

    sf::FloatRect half = rightHalf ? boardInnerRightRect() : boardInnerLeftRect();

    float cellW = half.size.x / (float)(COLS / 2); // 6 columns per half
    float cellH = half.size.y / (float)ROWS;       // 2 rows

    return sf::FloatRect(
        { half.position.x + localCol * cellW,
          half.position.y + row * cellH },
        { cellW, cellH }
    );
}

int SFMLRW::cellVal(int row, int col) const
{
    auto& b = g.GetBoardRef();
    return (int)b.at((size_t)row, (size_t)col);
}

std::optional<Coord> SFMLRW::hitTestCell(sf::Vector2f p) const
{
    // We do NOT hit-test against the full boardRect anymore; we only accept clicks
    // inside the inner left/right playable rectangles.

    auto left  = boardInnerLeftRect();
    auto right = boardInnerRightRect();

    const bool inLeft  = left.contains(p);
    const bool inRight = right.contains(p);

    if (!inLeft && !inRight)
        return std::nullopt;

    sf::FloatRect half = inLeft ? left : right;

    float cellW = half.size.x / (float)(COLS / 2);
    float cellH = half.size.y / (float)ROWS;

    int localCol = (int)((p.x - half.position.x) / cellW);
    int row      = (int)((p.y - half.position.y) / cellH);

    if (row < 0 || row >= ROWS || localCol < 0 || localCol >= (COLS / 2))
        return std::nullopt;

    int visualCol = localCol + (inRight ? (COLS / 2) : 0);

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

    sf::Text t(font);
    t.setString(s);
    t.setCharacterSize(size);
    t.setPosition({x, y});
    t.setFillColor(sf::Color::White);
    window.draw(t);
}

void SFMLRW::drawBoardGrid() const
{
    // Now: draw the board IMAGE as the background, and only draw overlays needed
    // (e.g., selection highlight). No grid coloring.

    auto br = boardRect();
    tryLoadBoardImage();

    if (boardSprite && boardTextureLoaded)
    {
        auto imgRect = boardImageRect();  // aspect-correct rect inside boardRect

        boardSprite->setPosition(imgRect.position);

        auto texSize = boardTexture.getSize();
        if (texSize.x > 0 && texSize.y > 0)
        {
            float sx = imgRect.size.x / (float)texSize.x;
            float sy = imgRect.size.y / (float)texSize.y;
            boardSprite->setScale({sx, sy});
        }

        window.draw(*boardSprite);
    }

    else
    {
        // Fallback background if image not present
        sf::RectangleShape bg(br.size);
        bg.setPosition(br.position);
        bg.setFillColor(sf::Color(35, 70, 45));
        window.draw(bg);
    }

    // Selection highlight overlay (same behavior as before, just drawn atop the board image)
    if (ctrl.StartIsSelected())
    {
        Coord sel = ctrl.GetStart();
        auto cr = cellRect(sel.row, sel.col);

        sf::RectangleShape hl(cr.size);
        hl.setPosition(cr.position);
        hl.setFillColor(sf::Color(120, 200, 120, 70));
        hl.setOutlineThickness(2.f);
        hl.setOutlineColor(sf::Color(120, 200, 120, 160));
        window.draw(hl);
    }

    // OPTIONAL: subtle inner playable bounds debugging (comment out if you want)
    // {
    //     auto l = boardInnerLeftRect();
    //     auto r = boardInnerRightRect();
    //     sf::RectangleShape a(l.size); a.setPosition(l.position);
    //     a.setFillColor(sf::Color(0,0,0,0)); a.setOutlineThickness(1.f); a.setOutlineColor(sf::Color(255,0,0,120));
    //     sf::RectangleShape b(r.size); b.setPosition(r.position);
    //     b.setFillColor(sf::Color(0,0,0,0)); b.setOutlineThickness(1.f); b.setOutlineColor(sf::Color(0,0,255,120));
    //     window.draw(a); window.draw(b);
    // }
}

void SFMLRW::drawPieces() const
{
    for (int r = 0; r < ROWS; ++r)
    {
        for (int c = 0; c < COLS; ++c)
        {
            int raw = cellVal(r, c);
            int count = std::abs(raw);
            if (count == 0) continue;

            int ownerSign = (raw > 0) ? 1 : -1;

            auto cr = cellRect(r, c);

            // Pieces are naturally kept away from the outer edges because cellRect is now inside inner play areas.
            float radius = std::min(cr.size.x, cr.size.y) * 0.18f;

            bool isTop = (r == 0);
            float baseX = cr.position.x + cr.size.x / 2.f;

            // Keep the same stacking logic, just relative to new cell rects
            float baseY = isTop ? (cr.position.y + radius + 10.f)
                                : (cr.position.y + cr.size.y - radius - 10.f);

            bool suppressed = activeAnim && 
                    (
                        (!activeAnim->isRemove && 
                        activeAnim->dstCell && 
                        Coord(r, c) == *activeAnim->dstCell) 
                    );

            int maxStack = std::min(count - suppressed, 5);
            if (maxStack <= 0)
                continue;

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

        bool gray = !g.CanUseDice(i) || ctrl.AwaitingRoll();

        if (gray)
        {
            die.setFillColor(sf::Color(170, 170, 170));
            die.setOutlineColor(sf::Color(80, 80, 80));
        }
        else
        {
            die.setFillColor(sf::Color(230, 230, 230));
            die.setOutlineColor(sf::Color(110, 110, 110));
        }

        die.setOutlineThickness(3.f);
        window.draw(die);

        int val = g.GetDice(i);

        if (fontLoaded)
        {
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
    hud.setFillColor(sf::Color(212, 180, 131)); //originally 18 18 18
    window.draw(hud);

    if (fontLoaded)
    {
        drawText(statusLine, margin, 18.f, 18);
        drawText("Click source point, then click die.", margin, 42.f, 16);
    }
}

sf::Vector2f SFMLRW::cellCenter(const Coord& c) const
{
    auto r = cellRect(c.row, c.col);
    return {
        r.position.x + r.size.x * 0.5f,
        r.position.y + r.size.y * 0.5f
    };
}

sf::Vector2f SFMLRW::pieceStackTopCenter(const Coord& c) const
{
    int raw = cellVal(c.row, c.col);
    int count = std::abs(raw);

    auto cr = cellRect(c.row, c.col);
    float radius = std::min(cr.size.x, cr.size.y) * 0.18f;

    bool isTop = (c.row == 0);
    float baseX = cr.position.x + cr.size.x / 2.f;

    float baseY = isTop
        ? (cr.position.y + radius + 10.f)
        : (cr.position.y + cr.size.y - radius - 10.f);

    int i = std::max(0, std::min(count - 1, 4)); // top visible piece
    float dy = (radius * 2.1f) * i;

    float y = isTop ? (baseY + dy) : (baseY - dy);
    return { baseX, y };
}

void SFMLRW::Render() const
{
    window.clear(sf::Color(15, 15, 15));
    drawHUD();
    drawBoardGrid();
    drawPieces();
    drawDice();

    if (game_over_screen)
        drawGameOverOverlay();

    else if (activeAnim)
    {
        auto& anim = *activeAnim;

        float t = animClock.getElapsedTime().asSeconds() / anim.duration;
        t = std::min(t, 1.f);

        if (t >= 1.f)
        {
            activeAnim.reset();
        }

        // ease-out cubic
        float u = 1.f - std::pow(1.f - t, 3.f);

        sf::Vector2f pos = anim.from + (anim.to - anim.from) * u;

        // draw animated piece
        float radius = 14.f; // reuse your existing radius logic if you want
        sf::CircleShape piece(radius);
        piece.setOrigin({radius, radius});
        piece.setPosition(pos);

        if (anim.ownerSign > 0)
            piece.setFillColor(sf::Color(220, 220, 220));
        else
            piece.setFillColor(sf::Color(40, 40, 40));

        piece.setOutlineThickness(2.f);
        piece.setOutlineColor(sf::Color(10,10,10));

        window.draw(piece);
    }

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
            if (game_over_screen)
            {
                game_over_screen = false;
                if (k->code != sf::Keyboard::Key::Enter) {
                    return ctrl.ReceiveCommand(Command(Actions::QUIT));
                }
                else {
                    return ctrl.ReceiveCommand(Command(Actions::RESTART));
                }
            }

            if (k->code == sf::Keyboard::Key::Q)
                return ctrl.ReceiveCommand(Command(Actions::QUIT));

            if (k->code == sf::Keyboard::Key::R)
                return ctrl.ReceiveCommand(Command(Actions::ROLL_DICE));

            if (k->code == sf::Keyboard::Key::P)
                return ctrl.ReceiveCommand(Command(Actions::RANDOM_AUTOPLAY));

            if (k->code == sf::Keyboard::Key::Num0 || k->code == sf::Keyboard::Key::Numpad0)
                return ctrl.ReceiveCommand(Command(false)); // die 0

            if (k->code == sf::Keyboard::Key::Num1 || k->code == sf::Keyboard::Key::Numpad1)
                return ctrl.ReceiveCommand(Command(true)); // die 1
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
                ctrl.ReceiveCommand(Actions::RELEASE_SELECTED);
                return ctrl.ReceiveCommand(Command(c->row, c->col));
            }
        }
    }

    Render();
    return status_codes::WAITING;
}

void SFMLRW::ReceiveGameEvent(const Game::Event& e)
{
    switch (e.code) 
    { 
        case Game::EventCode::QUIT: 
            statusLine = "Game ended by user."; 
            Render(); 
            window.close(); 
            return; 
        case Game::EventCode::GAME_OVER: 
            game_over_screen = true; 
            statusLine = "Game over!"; 
            Render(); 
            return; 
        case Game::EventCode::TURN_SWITCH: 
            InstructionMessage("New turn. Roll the dice."); 
            return; 
        case Game::EventCode::DICE_ROLL: 
            Render(); 
            return; 
        case Game::EventCode::MOVE: 
        { 
            const auto& md = std::get<Game::MoveData>(e.data); 
            PieceAnimation anim; 
            anim.from = pieceStackTopCenter(md.from); 
            anim.to = pieceStackTopCenter(md.to); 
            anim.isRemove = false; 
            anim.srcCell = md.from; 
            anim.dstCell = md.to; 
            anim.ownerSign = g.GetBoardRef().PlayerSign();

            activeAnim = anim; 
            animClock.restart(); 

            while(activeAnim)   // fix later
                Render();

            return; 
        } 

        case Game::EventCode::REMOVE: 
        { 
            const auto& rd = std::get<Game::RemoveData>(e.data); 
            PieceAnimation anim; 
            anim.from = pieceStackTopCenter(rd._from); 
            // Animate toward "off-board" direction 
            auto off = anim.from; off.y += (rd._from.row == 0 ? -80.f : 80.f); 
            anim.to = off;
            anim.isRemove = true; 
            anim.srcCell = rd._from; anim.dstCell.reset(); 
            anim.ownerSign = g.GetBoardRef().PlayerSign();

            activeAnim = anim; 
            animClock.restart(); 

            while(activeAnim)   // fix later
                Render();

            return; 
        } 
        default: 
            Render(); 
            return; 
    } 
}

void SFMLRW::drawGameOverOverlay() const
{
    sf::RectangleShape overlay({(float)W, (float)H});
    overlay.setPosition({0.f, 0.f});
    overlay.setFillColor(sf::Color(0, 0, 0, 180));
    window.draw(overlay);

    if (!fontLoaded)
        return;

    float cx = W * 0.5f;

    sf::Text title(font);
    title.setString("GAME OVER");
    title.setCharacterSize(48);
    title.setFillColor(sf::Color::White);
    auto tb = title.getLocalBounds();
    title.setOrigin({tb.size.x / 2.f, tb.size.y / 2.f});
    title.setPosition({cx, H * 0.35f});
    window.draw(title);

    sf::Text prompt(font);
    prompt.setString("Press ENTER to play again\nPress any other key to quit");
    prompt.setCharacterSize(20);
    prompt.setFillColor(sf::Color(220, 220, 220));
    auto pb = prompt.getLocalBounds();
    prompt.setOrigin({pb.size.x / 2.f, pb.size.y / 2.f});
    prompt.setPosition({cx, H * 0.55f});
    window.draw(prompt);
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

} // namespace Nardi
