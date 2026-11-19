# Quick Picks recommendations

Quick Picks now uses `QuickPicksRecommendationEngine` for retrieval and the pure,
deterministic `QuickPicksRecommender` for learning and ranking. The top carousel
still shows recent listening; the Quick Picks discovery list uses the new engine.

## Taste and feedback

- Read up to 2,000 listening events and use events from the last 90 days. Completion
  quality is `(played / duration)^2`, capped at one; unknown duration uses 180 seconds.
  These are listening-time estimates, not proof that the whole recording was heard.
- Cap a song's daily contribution and apply logarithmic saturation to repeats.
  Blend long-term taste (30-day half-life) with recent taste (3-day half-life).
- Likes provide a cold-start signal. Their profile weight drops from 65% to 15%
  as history reaches 12 distinct songs. Imported likes cannot outweigh established
  listening habits just because there are many of them.
- Early forward track skips now write to the existing `song_skip` table. Only
  cross-track seek transitions between 1 second and the smaller of 60 seconds or
  35% duration count. Natural completion, scrubbing, queue replacement, remote
  handover and shared-session guests do not count. Paused listening history also
  disables this feedback. Skips before this change were not wired to the player.
- Repeated skips reduce a song's eligibility, decaying with a 14-day half-life;
  successful listening offsets them. A skip does not blacklist an entire artist.
  The existing table stores a count and last timestamp, so individual skip ages
  cannot be reconstructed.

## Retrieval and ranking

Select up to six high-quality seeds, at most two per artist. Last-listen mode uses
the latest sufficiently listened-to, non-rejected seed. Local file IDs are not
sent to YouTube as seeds. Fetch each seed's related-song shelf plus the existing
YouTube Music Charts endpoint. Charts follow that endpoint's locale/account
behavior; this engine does not invent regional or global trend data.

Related-song requests run with three-way concurrency and a 3.5-second timeout per
seed; charts have a separate four-second timeout. Individual failures or timeouts
leave other sources usable. At most 60 results per seed are considered.

Merge candidates by ID and retain one vote per seed. Ranking combines the strongest
related-song vote (52%), artist affinity (28%), independent-seed consensus (12%),
and a trend boost (up to 16%, multiplied by taste fit). Small novelty, listening
fatigue and previous-display adjustments follow, plus a skip penalty. These are
heuristic scores, not calibrated probabilities. With an established profile, a
candidate needs artist or related-song evidence before popularity can help it.
With no profile, charts provide the starting recommendations.

Discovery excludes liked songs and the last 30 distinct listened-to songs. A
greedy diversity pass penalizes artist/album overlap, limits each artist (including
featured artists) to three results, and removes duplicate uploads with the same
artist/title after stripping basic official-video/audio/lyrics suffixes. A short
relevant list is preferable to filling it with unrelated chart entries.

The local fallback uses the same taste and skip policy with known songs allowed.
Explicit/video preferences filter online candidates before ranking. The cache
has a version, six-hour expiry, and preserved video-type metadata; old algorithm
caches are invalidated. A fresh load replaces cached picks in the same session.

## Validation and limits

Run the deterministic policy tests with:

```sh
./gradlew :app:testMobileArm64DebugUnitTest -PconnectTestBuildType=debug --tests com.nikhil.yt.utils.QuickPicksRecommenderTest
```

Tests cover likes versus actual listening, completion, recency, saturation,
diverse seeds, last-listen mode, incompatible trends, cold starts, graph discovery,
independent votes, skip recovery, discovery exclusions, duplicate uploads and
artist limits. No schema migration or new external service is needed.

This is a behavioral and related-song recommender, not an audio embedding model:
the catalog does not supply reliable genre/tempo/language features here. Artist
names are Unicode-normalized, but aliases in different scripts can still split
affinity. Actual improvement needs listening evaluation on a device: compare early
skip rate, substantial listens, saves and unique artists across comparable sessions.
The tests verify policy behavior, not a claimed percentage improvement in taste.

## Experimental personalized radio

On `feat/personalized-radio`, song radio, its continuation pages, Start Radio,
and automatic queue extensions use this same ranking policy. The selected song
anchors the session with weight 1.0; up to two historical seeds contribute at
half their usual weight. Existing YouTube radio results supply current-song
evidence without requesting the same radio again. Charts are cached in memory
for 15 minutes per account/locale.

Radio allows liked songs, excludes tracks already in the queue, and retains skip
penalties and artist diversity. Explicit playlist and album order is preserved.
Personalization has a five-second budget, with filtered YouTube results used on
timeout or failure. A queue revision and current-song check discard stale results;
autofill waits for the initial radio queue to finish loading.

Downloads recovered from `Music/Velune` now rebuild missing library rows from
local MP3 metadata (title, artist, album and duration), with a filename fallback.
Existing rows, likes and history are preserved. This runs on startup, after audio
permission is granted, and when the Downloaded list is refreshed; account sync
and network access are not required. Android audio permission is still needed to
read files owned by an earlier installation.

The branch's Debug workflow runs `QuickPicksRecommenderTest`,
`RadioQueuePolicyTest`, and `DownloadedSongRecoveryTest` before building the APK.
Device checks should cover rapid song changes, Start Radio, repeat/autoplay
toggles, playlist order, and recovered downloads before login with audio
permission granted. File scanning, Android permissions and playback behavior
need device verification in addition to the deterministic unit tests.
