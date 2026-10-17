# App Store privacy disclosure — anonymous game history

Covers the anonymous cloud upload of finished games (issue #1). Use the checklist
when answering App Store Connect → App Privacy, and publish the policy text at the
app's Privacy Policy URL before submitting a build that uploads games.

## App Privacy label checklist

App Store Connect → your app → **App Privacy** → Get Started / Edit.

- [ ] **Do you or your third-party partners collect data from this app?** → Yes
- [ ] Data type: **User Content → Gameplay Content**
  - [ ] Purpose: **Analytics** (evaluate how the game is played, tune bots, find rule bugs).
        Add **App Functionality** later only if online multiplayer uses it.
  - [ ] **Linked to the user's identity?** → Yes — each game carries the install's
        anonymous ID, so an install's games can be grouped.
  - [ ] **Used for tracking?** → No — never combined with third-party data or shared
        with data brokers / ad networks.
- [ ] Data type: **Identifiers → User ID** — the anonymous install ID is an
      app-scoped account ID. Apple's definition covers it, so declare it with the same
      answers: Analytics · Linked · Not used for tracking.
- [ ] Do **not** declare Name, Email, Contact Info, Device ID, Location, Usage Data
      or Diagnostics: none are sent.
- [ ] **Privacy Policy URL** set to the published copy of the policy below.
- [ ] Publish the label, then confirm the entries on the app's product page
      preview.
- [ ] Developer confirms the label entries were submitted (issue #6).

## Privacy policy (draft)

> **Nardi — Privacy Policy**
> *Last updated: October 16, 2026*
>
> **What we collect.** When a game finishes — by being played out or by
> resignation — Nardi uploads a record of that game: the dice and moves of every
> turn, who moved first, the result, whether it ended by resignation, the kind of
> opponent (which computer level, or a human on the same device), the computer
> opponent's model version, the app version and build, and the time the game
> finished. Games you quit before the end are not uploaded.
>
> **How it's anonymous.** The first time the app uploads, it creates a random
> anonymous ID for your installation. Your games are stored with that ID so they
> can be grouped together, but it is not tied to your name, email, Apple ID, device
> name or any other personal information, and we never ask for any. Deleting the
> app discards the ID; a reinstall gets a new one.
>
> **Why.** To understand how the game is played so we can improve it: measure and
> tune the computer opponents, find rule bugs, and study things like first-move
> advantage.
>
> **What we don't do.** We don't sell or share game data, use it for advertising,
> track you across other apps or websites, or combine it with data from other
> sources.
>
> **Where it's stored.** Game records are stored with our database provider,
> Supabase (supabase.com), which processes them on our behalf. As with any internet
> service, Supabase may briefly log the network address a request came from; we do
> not use it to identify you.
>
> **Your choice.** You can turn off **Settings → Share anonymous game data** at any
> time. Games finished while it is off are never uploaded — not even if you turn it
> back on later. Your match history on the device keeps working either way.
>
> **Contact.** Questions or deletion requests: nardi.master0@gmail.com. Include your
> approximate play dates; because the data is anonymous we may not be able to
> identify your games without the in-app ID.

### Before publishing

- Settings toggle label above must match the shipped copy (issue #5).
- Re-check the collected fields against the uploader once issue #4 lands.

## Editing the published policy

The live copy is https://haykgar.github.io/LongNardi/privacy/, served by GitHub
Pages from `privacy/index.html` on the `gh-pages` branch. That branch has no shared
history with `main` and holds only the policy page (plus `.nojekyll`).

The text exists twice, so any change goes to both:

1. Edit the draft above on `main`, and bump *Last updated*.
2. Mirror the change into `privacy/index.html` on `gh-pages`, including the date:
   ```sh
   git worktree add ../nardi-pages gh-pages
   # edit ../nardi-pages/privacy/index.html
   git -C ../nardi-pages commit -am "Update privacy policy"
   git -C ../nardi-pages push
   git worktree remove ../nardi-pages
   ```
3. Wait about a minute, then check the live URL shows the new text.

Keep the URL stable: App Store Connect points at it. If it ever has to move, update
**App Privacy → Privacy Policy URL** at the same time.
