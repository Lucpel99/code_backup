// Copyright 2019 DeepMind Technologies Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef OPEN_SPIEL_GAMES_COUNTER_AIR_H_
#define OPEN_SPIEL_GAMES_COUNTER_AIR_H_

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "open_spiel/spiel.h"

// Simple game of Noughts and Crosses:
// https://en.wikipedia.org/wiki/Tic-tac-toe
//
// Parameters: none

namespace open_spiel {
namespace counter_air {

// Constants.
inline constexpr int kNumPlayers = 2;
inline constexpr int kMaxCountersPerBox = 10;
inline constexpr int kNumBoxes = 9;  // The amount of boxes the game pieces may be placed in.

// State of an in-play game.
class CounterAirState : public State {
   public:
    CounterAirState(std::shared_ptr<const Game> game);

    CounterAirState(const CounterAirState &) = default;
    CounterAirState &operator=(const CounterAirState &) = default;

    Player CurrentPlayer() const override {
        return IsTerminal() ? kTerminalPlayerId : current_player_;
    }
    std::string ActionToString(Player player, Action action_id) const override;
    std::string ToString() const override;
    bool IsTerminal() const override;
    std::vector<double> Returns() const override;
    std::string InformationStateString(Player player) const override;
    std::string ObservationString(Player player) const override;
    void ObservationTensor(Player player,
                           absl::Span<float> values) const override;
    std::unique_ptr<State> Clone() const override;
    void UndoAction(Player player, Action move) override;
    std::vector<Action> LegalActions() const override;

    Player outcome() const { return outcome_; }

    // protected:
    std::array<int, 18> board_;
    std::array<int, 18> board_zero_;  // Let board be zerod at phase 0.

    void DoApplyAction(Action move) override;

    // private:
    bool FinalRoundEnd() const;  // Is the final round finished?
    Player current_player_ = 0;  // Player zero goes first
    Player outcome_ = kInvalidPlayer;
    int current_wave_ = 0;
    int num_moves_ = 0;
    int current_phase_ = 0;
    int blue_hits_ = 0;
    int red_hits_ = 0;
    int blue_points_ = 0;
    int red_points_ = 0;
    int blue_placeable_fighters_ = 10;
    int red_placeable_fighters_ = 4;
    int red_placeable_sams_ = 4;
    int attacking_box_ = 8;  // the first attack is in index 8, the red intercept box
    int low_strike_attacks_ = 0;
    int max_low_strike_attacks_ = 0;
    int active_sam_attacks_ = 0;
    int max_active_sam_attacks_ = 0;
    int passive_sam_attacks_ = 0;
    int max_passive_sam_attacks_ = 0;
    int airbase_attacks_ = 0;
    int max_airbase_attacks_ = 0;
    bool is_uav_ = true;
    bool is_attacking_ = true;
};

// Game object.
class CounterAirGame : public Game {
   public:
    explicit CounterAirGame(const GameParameters &params);
    int NumDistinctActions() const override { return 13; }  // DUBBELKOLLA
    std::unique_ptr<State> NewInitialState() const override {
        return std::unique_ptr<State>(new CounterAirState(shared_from_this()));
    }
    int NumPlayers() const override { return kNumPlayers; }
    double MinUtility() const override { return -1; }
    absl::optional<double> UtilitySum() const override { return 0; }
    double MaxUtility() const override { return 1; }
    std::vector<int> ObservationTensorShape() const override {
        return {246};  // ÄNDRA
    }
    int MaxGameLength() const override { return 1000; }
    std::string ActionToString(Player player, Action action_id) const override;
};

std::string PlayerToString(Player player);

inline std::ostream &operator<<(std::ostream &stream, const int &state) {
    return stream << PlayerToString(state);
}

}  // namespace counter_air
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_COUNTER_AIR_H_
