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
// ./open_spiel/scripts/build_and_run_test
#include "open_spiel/games/counter_air.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "open_spiel/spiel_utils.h"
#include "open_spiel/utils/tensor_view.h"

namespace open_spiel {
namespace counter_air {
namespace {

// Facts about the game.
const GameType kGameType{
    /*short_name=*/"counter_air",
    /*long_name=*/"Counter Air",
    GameType::Dynamics::kSequential,
    GameType::ChanceMode::kDeterministic,
    GameType::Information::kPerfectInformation,
    GameType::Utility::kZeroSum,
    GameType::RewardModel::kTerminal,
    /*max_num_players=*/2,
    /*min_num_players=*/2,
    /*provides_information_state_string=*/true,
    /*provides_information_state_tensor=*/false,
    /*provides_observation_string=*/true,
    /*provides_observation_tensor=*/true,
    /*parameter_specification=*/{}  // no parameters
};

std::shared_ptr<const Game> Factory(const GameParameters &params) {
    return std::shared_ptr<const Game>(new CounterAirGame(params));
}

REGISTER_SPIEL_GAME(kGameType, Factory);

}  // namespace

std::string PlayerToString(Player player) {
    switch (player) {
        case 0:
            return "Blue";
        case 1:
            return "Red";
        default:
            SpielFatalError(absl::StrCat("Invalid player id ", player));
    }
}

std::string StateToString(int state) {
    return std::to_string(state);
}

// -----board indexes-----
// 0-1: Escort
// 2-3: High Strike
// 4-5: SEAD
// 6-7: Low Strike
// 8-9: Intercept
// 10-11: Active SAM
// 12-13: Passive SAM
// 14-15: Airbase
// 16-17: AAA

void CounterAirState::DoApplyAction(Action move) {
    if (move == 11) {  // No legal action, and the players turn is changed.
        current_player_ = 1 - current_player_;
        num_moves_++;
        is_attacking_ = true;
        if (num_moves_ > 200) {  // LOOP
            SpielFatalError(absl::StrCat("Invalid player id ", current_player_));
        }
        return;
    }
    if (move == 12) {  // No legal action, next phase
        if (current_phase_ == 5) {
            max_low_strike_attacks_ = std::min(board_[6], 4);
        }
        if (current_phase_ == 6) {
            max_active_sam_attacks_ = board_[10] + board_[11];
            max_passive_sam_attacks_ = board_[12] + board_[13];
            max_airbase_attacks_ = board_[14] + board_[15];
        }

        if (current_phase_ == 9) {  // Next wave
            current_phase_ = 0;
            current_wave_++;
            low_strike_attacks_ = 0;
            active_sam_attacks_ = 0;
            passive_sam_attacks_ = 0;
            airbase_attacks_ = 0;

            red_placeable_fighters_ = board_[8] + board_[9] + board_[14];
            for (int i = 0; i <= 7; i++) {
                blue_placeable_fighters_ += board_[i];
            }
            for (int i = 0; i <= 3; i++) {
                red_placeable_sams_ += board_[i + 10];
            }
            int fighters_in_airbase = board_[15];
            std::fill(board_.begin(), board_.end(), 0);
            board_[14] = fighters_in_airbase;

        } else {
            current_phase_++;
        }
        is_attacking_ = true;
        current_player_ = 0;

        if (CounterAirState::FinalRoundEnd()) {
            if (blue_points_ > red_points_ + 2) {
                outcome_ = 0;
            } else if (blue_points_ == red_points_ + 2) {
                if (blue_hits_ > red_hits_) {
                    outcome_ = 0;
                } else if (blue_hits_ == red_hits_) {
                    outcome_ = -1;
                } else {
                    outcome_ = 1;
                }
            } else {
                outcome_ = 1;
            }
        }
        return;
    }

    switch (current_phase_) {
        case 0:  // Place Escort
            ////std::cout << "Escort  ";
            board_[0] = move;
            blue_placeable_fighters_ -= move;
            current_phase_++;
            break;

        case 1:  // Place High Strike
            ////std::cout << "High Strike  ";
            board_[2] = move;
            blue_placeable_fighters_ -= move;
            current_phase_++;
            break;

        case 2:  // Place SEAD/Low Strke
            ////std::cout << "SEAD/Low Strike  ";
            board_[4] = move;
            blue_placeable_fighters_ -= move;
            board_[6] = blue_placeable_fighters_;
            blue_placeable_fighters_ = 0;
            num_moves_++;
            current_player_ = 1 - current_player_;
            current_phase_++;
            break;

        case 3:  // Place Intercept/Airbase
            ////std::cout << "Intercept/Airbase  ";
            board_[8] = move;  // If the 9th place in the array is the number of fighters in intercept box
            red_placeable_fighters_ -= move;
            board_[14] = red_placeable_fighters_;  // 11th place in the array
            red_placeable_fighters_ = 0;
            current_phase_++;
            break;

        case 4:  // Place Active/Passive SAM
            ////std::cout << "SAMS  ";
            board_[10] = move;
            red_placeable_sams_ -= move;
            board_[12] = red_placeable_sams_;
            red_placeable_sams_ = 0;
            board_[16] = 4;
            current_player_ = 1 - current_player_;
            current_phase_++;
            num_moves_++;
            break;

        case 5:  // fighter-fighter combat
            ////std::cout << "Fighter-Fighter combat  ";
            if (current_player_ == 0) {  // If the current player is the blue side
                if (is_attacking_) {     // Blue player fires its first missile at red
                    if (move == 1) {
                        attacking_box_ = 8;
                        board_[0]--;
                        board_[1]++;
                    }
                    is_attacking_ = false;  // reds turn To defend
                    current_player_ = 1;
                } else {
                    if (move == 0) {  // Blue does nothing
                        blue_hits_ += 2;
                        if (blue_hits_ > 4) {
                            blue_hits_ -= 4;
                            board_[attacking_box_]--;
                            red_points_++;
                        }
                    } else if (move == 1) {  // Escort evades, and 1 damage is dealt to the attacking box.
                        blue_hits_++;
                        if (blue_hits_ >= 4) {
                            blue_hits_ -= 4;
                            board_[attacking_box_]--;
                            red_points_++;
                        }
                        board_[0]--;
                        board_[1]++;
                    } else if (move == 2 || move == 3) {  // fighter in the high strike/low strike box chooses to evade, taking only 1 hit and preventing further attacks from this fighter in high-strike
                        blue_hits_++;
                        if (blue_hits_ >= 4) {
                            blue_hits_ -= 4;
                            board_[attacking_box_]--;
                            red_points_++;
                        } else {
                            board_[attacking_box_]--;
                            board_[attacking_box_ + 1]++;
                        }
                    }
                    is_attacking_ = true;
                }
            } else if (current_player_ == 1) {
                if (is_attacking_) {
                    if (move == 0) {
                        attacking_box_ = 0;
                    }
                    if (move == 1) {
                        attacking_box_ = 2;
                    }
                    if (move == 2) {
                        attacking_box_ = 6;
                    }
                    is_attacking_ = false;
                    board_[8]--;
                    board_[9]++;
                    current_player_ = 0;
                } else {
                    if (move == 1) {  // red player chooses to evade
                        red_hits_++;  // blue scores 1 hit
                        if (red_hits_ >= 4) {
                            red_hits_ -= 4;
                            board_[8]--;
                            blue_points_++;
                        } else {
                            board_[8]--;
                            board_[9]++;
                        }
                    } else if (move == 0) {
                        red_hits_ += 2;
                        if (red_hits_ >= 4) {
                            red_hits_ -= 4;
                            board_[8]--;
                            blue_points_++;
                        }
                    }
                    is_attacking_ = true;
                }
            }
            num_moves_++;
            break;

        case 6:
            // std::cout << "Ground-to-Air combat  ";
            if (current_player_ == 0) {  // If the current player is the blue side
                if (is_attacking_) {     // Blue player fires its first missile at red
                    if (move == 0) {
                        attacking_box_ = 10;
                    }
                    if (move == 1) {  // Flips AAA
                        attacking_box_ = 16;
                    }
                    board_[4]--;
                    board_[5]++;

                    is_attacking_ = false;
                    current_player_ = 1;
                } else {              // Blue defends
                    if (move == 0) {  // Blue does nothing
                        blue_hits_ += 2;
                        if (blue_hits_ >= 4) {
                            blue_hits_ -= 4;
                            board_[2]--;
                            red_points_++;
                        }
                    } else if (move == 1) {  // High Strike evades and takes 1 damage
                        blue_hits_++;
                        if (blue_hits_ >= 4) {
                            blue_hits_ -= 4;
                            board_[2]--;
                            red_points_++;
                        } else {
                            board_[2]--;
                            board_[3]++;
                        }
                    } else if (move == 2) {  // SEAD tries to supress the SAMS, and so only 1 damage is taken by the High-strike fighter.
                        blue_hits_++;
                        if (blue_hits_ >= 4) {
                            blue_hits_ -= 4;
                            board_[2]--;
                            red_points_++;
                        }
                        board_[4]--;
                        board_[5]++;
                    } else if (move == 3) {
                        blue_hits_++;
                        if (blue_hits_ >= 4) {
                            blue_hits_ -= 4;
                            board_[6]--;
                            red_points_++;
                        }
                    }
                    is_attacking_ = true;
                }
            } else if (current_player_ == 1) {  // Red attacks with its active SAMS, and AAA.
                if (is_attacking_) {
                    if (move == 0) {
                        attacking_box_ = 2;
                        is_attacking_ = false;
                        board_[10]--;
                        board_[11]++;
                    }
                    if (move == 1) {
                        attacking_box_ = 6;  // AAA attacks
                        low_strike_attacks_++;
                        board_[16]--;
                        board_[17]++;
                    }
                    current_player_ = 0;
                } else {  // Blue players attack determines the reds defence. no move is really taken here.
                    if (move == 0) {
                        if (attacking_box_ == 10) {
                            red_hits_++;
                            if (red_hits_ >= 4) {
                                red_hits_ -= 4;
                                board_[10]--;
                                blue_points_++;
                            } else {
                                board_[10]--;
                                board_[11]++;
                            }
                        } else if (attacking_box_ == 16) {
                            board_[16]--;
                            board_[17]++;
                        }
                        is_attacking_ = true;
                    }
                }
            }
            num_moves_++;
            break;

        case 7:
            // std::cout << "Air-to-Ground combat, CASE 7";
            red_hits_++;

            if (move == 0) {  // Any fighter in airbase

                airbase_attacks_++;
                if (board_[14] == 0) {
                    attacking_box_ = 15;
                } else {
                    attacking_box_ = 14;
                }
            }
            if (move == 1) {  // Any sam in active SAMS

                active_sam_attacks_++;
                if (board_[10] == 0) {
                    attacking_box_ = 11;
                } else {
                    attacking_box_ = 10;
                }
            }
            if (move == 2) {  // Any sam in passive SAMS

                passive_sam_attacks_++;
                if (board_[12] == 0) {
                    attacking_box_ = 13;
                } else {
                    attacking_box_ = 12;
                }
            }
            if (red_hits_ >= 4) {
                red_hits_ -= 4;
                board_[attacking_box_]--;
                blue_points_++;
            }
            board_[2]--;
            board_[3]++;
            break;

        case 8:
            // std::cout << "Ground-to-Air combat, CASE 8";
            red_hits_++;
            if (move == 0) {  // Any sam in active SAMS
                active_sam_attacks_++;
                if (board_[10] == 0) {
                    attacking_box_ = 11;
                } else {
                    attacking_box_ = 10;
                }
            }
            if (move == 1) {  // Any sam in passive SAMS
                passive_sam_attacks_++;
                if (board_[12] == 0) {
                    attacking_box_ = 13;
                } else {
                    attacking_box_ = 12;
                }
            }
            if (red_hits_ >= 4) {
                red_hits_ -= 4;
                board_[attacking_box_]--;
                blue_points_++;
            }
            current_phase_++;
            break;

        case 9:
            // std::cout << "Ground-to-Air combat, CASE 9";
            if (move == 0) {
                board_[14]--;
                board_[15]++;
            }
            if (move == 1) {
                red_hits_++;
                active_sam_attacks_++;
                if (board_[10] == 0) {
                    attacking_box_ = 11;
                } else {
                    attacking_box_ = 10;
                }
                if (red_hits_ >= 4) {
                    red_hits_ -= 4;
                    board_[attacking_box_]--;
                    blue_points_++;
                }
            }
            if (move == 2) {
                red_hits_++;
                passive_sam_attacks_++;
                if (board_[12] == 0) {
                    attacking_box_ = 13;
                } else {
                    attacking_box_ = 12;
                }
                if (red_hits_ >= 4) {
                    red_hits_ -= 4;
                    board_[attacking_box_]--;
                    blue_points_++;
                }
            }
            if (move == 3) {
                if (board_[8] > 0) {
                    board_[8]--;  // Intercept
                } else {
                    board_[9]--;
                }
                board_[15]++;  // E Airbase
            }
            board_[6]--;
            board_[7]++;
            break;
    }
}

// -----board indexes-----
// 0-1: Escort
// 2-3: High Strike
// 4-5: SEAD
// 6-7: Low Strike
// 8-9: Intercept
// 10-11: Active SAM
// 12-13: Passive SAM
// 14-15: Airbase
// 16-17: AAA

std::vector<Action> CounterAirState::LegalActions() const {
    if (IsTerminal())
        return {};

    std::vector<Action> moves;
    // if ((board_[0]==0 && board_[1]==0 && board_[2]==0 && board_[3]==0 && board_[4]==0 && board_[5]==0 && board_[6]==0 && board_[7]==0)) {}
    switch (current_phase_) {
        case 0:  // Place Escort
            // std::cout << "CASE 0  ";
            for (int i; i <= blue_placeable_fighters_; i++) {
                moves.push_back(i);
            }
            break;

        case 1:  // Place High Strike
            // std::cout << "CASE 1  ";
            for (int i; i <= blue_placeable_fighters_; i++) {
                moves.push_back(i);
            }
            break;

        case 2:  // Place SEAD/Low Strke
            // std::cout << "CASE 2  ";
            for (int i; i <= blue_placeable_fighters_; i++) {
                moves.push_back(i);
            }
            break;

        case 3:  // Place Intercept/Airbase
            // std::cout << "CASE 3  ";
            for (int i; i <= red_placeable_fighters_; i++) {
                moves.push_back(i);
            }
            break;

        case 4:  // Place Active/Passive SAM
            // std::cout << "CASE 4  ";
            for (int i; i <= red_placeable_sams_; i++) {
                moves.push_back(i);
            }
            break;

        case 5:  // Fighter-Figher Combat
            // std::cout << "CASE 5  ";
            if (current_player_ == 0) {
                if (is_attacking_) {                       // If its the first blue fighter, it always atatcks "_first_strike = True"
                    if (board_[0] > 0 && board_[8] > 0) {  // If there exists both fighters in the intercept and escort box and there exists "Attacking" in the escort fighters
                        moves.push_back(1);                // Player Blue has the option of either attacking
                    }
                }

                else {
                    moves.push_back(0);  // do nothing and loose 2 health, and retain the ability to strike the enemy

                    if (board_[0] > 0) {  // evade with escort and loose 1 health, and loose the opportunity to strike the enemy. Only if blue has any escorts left!
                        moves.push_back(1);
                    }
                    if (attacking_box_ == 2) {
                        moves.push_back(2);
                    }
                    if (attacking_box_ == 6) {
                        moves.push_back(3);
                    }
                }
            }
            if (current_player_ == 1) {
                if (is_attacking_) {
                    if (board_[8] > 0 && board_[0] > 0) {
                        moves.push_back(0);
                    }
                    if (board_[8] > 0 && board_[2] > 0) {
                        moves.push_back(1);
                    }
                    if (board_[8] > 0 && board_[6] > 0) {
                        moves.push_back(2);
                    }
                } else {
                    moves.push_back(0);  // Do nothing
                    moves.push_back(1);  // Evade
                }
            }
            if (moves.empty() && (board_[8] == 0 || (board_[0] == 0 && board_[2] == 0 && board_[6] == 0))) {
                moves.push_back(12);
            }
            break;

        case 6:  // Ground-to-Air Combat
            // std::cout << "CASE 6  ";
            if (current_player_ == 0) {
                if (is_attacking_) {      // If its the first blue fighter, it always atatcks "_first_strike = True"
                    if (board_[4] > 0) {  // If there exists both fighters in the intercept and escort box and there exists "Attacking" in the escort fighters
                        if (board_[10] > 0) {
                            moves.push_back(0);  // Blue attacks Active SAM
                        }
                        if (board_[16] > 0) {
                            moves.push_back(1);  // Blue attacks AAA
                        }
                    }
                } else {
                    if (attacking_box_ == 2) {
                        moves.push_back(0);   // do nothing and loose 2 health, and retain the ability to strike the enemy
                        moves.push_back(1);   // Evade with high-strike
                        if (board_[4] > 0) {  // evade with SEAD and loose 1 health, and loose the opportunity to strike the enemy. Only if blue has any SEAD left!
                            moves.push_back(2);
                        }
                    }
                    if (attacking_box_ == 6) {  // Red AAA attacks low strike
                        moves.push_back(3);
                    }
                }
            }

            if (current_player_ == 1) {  //
                if (is_attacking_) {
                    if (board_[10] > 0 && board_[2] > 0) {  // There are active, attacking SAMS ready to fire upon the active fighters in the high-strike box
                        moves.push_back(0);
                    }
                    if (board_[16] > 0 && board_[6] > 0 && low_strike_attacks_ < max_low_strike_attacks_) {  // Or active AAA
                        moves.push_back(1);
                    }
                } else {
                    moves.push_back(0);  // Do nothing
                }
            }
            if ((board_[10] == 0 || board_[2] == 0) && ((board_[16] == 0 || board_[6] == 0) || (low_strike_attacks_ == max_low_strike_attacks_)) && (board_[4] == 0 || (board_[10] == 0 && board_[16] == 0))) {  // No moves available. Change phase
                moves.push_back(12);
            }
            break;

        case 7:  // Air-to-Ground Combat High Strike
            // std::cout << "CASE 7  ";
            if (board_[2] > 0) {  // If there exists both fighters in the intercept and escort box and there exists "Attacking" in the escort fighters
                if ((board_[14] > 0 || board_[15] > 0) && (airbase_attacks_ < max_airbase_attacks_)) {
                    moves.push_back(0);  // Blue attacks Aribase
                }
                if ((board_[10] > 0 || board_[11] > 0) && (active_sam_attacks_ < max_active_sam_attacks_)) {
                    moves.push_back(1);  // Blue attacks Active SAM
                }
                if ((board_[12] > 0 || board_[13] > 0) && (passive_sam_attacks_ < max_passive_sam_attacks_)) {
                    moves.push_back(2);  // Blue attacks Passive SAM
                }
            }
            if (moves.empty()) {
                moves.push_back(12);
            }
            break;

        case 8:  // UAV
            // std::cout << "CASE 8  ";
            if (current_wave_ == 0 || current_wave_ == 2) {
                if ((board_[10] > 0 || board_[11] > 0) && (active_sam_attacks_ < max_active_sam_attacks_)) {
                    moves.push_back(0);  // Blue attacks Active SAM
                }
                if ((board_[12] > 0 || board_[13] > 0) && (passive_sam_attacks_ < max_passive_sam_attacks_)) {
                    moves.push_back(1);  // Blue attacks Passive SAM
                }
            }
            if (moves.empty()) {
                moves.push_back(12);
            }
            break;

        case 9:  // Air-to-Ground Combat Low Strike
            // std::cout << "CASE 9  ";
            //  If its the first blue fighter, it always atatcks "_first_strike = True"
            if (board_[6] > 0) {  // If there exists both fighters in the intercept and escort box and there exists "Attacking" in the escort fighters
                if (board_[14] > 0) {
                    moves.push_back(0);  // Blue low strike flips red attacking fighters in airbase to evading
                }
                if ((board_[10] > 0 || board_[11] > 0) && (active_sam_attacks_ < max_active_sam_attacks_)) {
                    moves.push_back(1);  // Blue attacks A/E Active SAM
                }
                if ((board_[12] > 0 || board_[13] > 0) && (passive_sam_attacks_ < max_passive_sam_attacks_)) {
                    moves.push_back(2);  // Blue attacks A/E Passive SAM
                }
                if (board_[8] > 0 || board_[9] > 0) {
                    moves.push_back(3);  // Blue low-strike puts a intercepting fighter into the airbase in evading status for the next wave.
                }
            }
            if (moves.empty()) {  // Either blue has no attacking low-strike fighters, or the low strike may not have any targets left.
                moves.push_back(12);
            }

            break;
    }
    if (moves.empty()) {
        moves.push_back(11);  // No moves available. Change player
    }
    return moves;
}

std::string CounterAirState::ActionToString(Player player,
                                            Action action_id) const {
    return game_->ActionToString(player, action_id);
}

// bool CounterAirState:: HasLine(Player player) const {
//   return BoardHasLine(board_, player);
// }

bool CounterAirState::FinalRoundEnd() const { return current_wave_ == 5; }

CounterAirState::CounterAirState(std::shared_ptr<const Game> game) : State(game) {
    std::fill(begin(board_), end(board_), 0);
}

std::string CounterAirState::ToString() const {
    std::string str;
    absl::StrAppend(&str, "┌──┬──┬──┐\n");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, std::to_string(board_[0]));
    absl::StrAppend(&str, std::to_string(board_[1]));
    absl::StrAppend(&str, "│  │  │\n");
    absl::StrAppend(&str, "├──┤");
    absl::StrAppend(&str, std::to_string(board_[2]));
    absl::StrAppend(&str, std::to_string(board_[3]));
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, std::to_string(board_[8]));
    absl::StrAppend(&str, std::to_string(board_[9]));
    absl::StrAppend(&str, "│\n");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, std::to_string(board_[4]));
    absl::StrAppend(&str, std::to_string(board_[5]));
    absl::StrAppend(&str, "│  │  │\n");
    absl::StrAppend(&str, "├──┴──┼──┤\n");
    absl::StrAppend(&str, "│ ");
    absl::StrAppend(&str, std::to_string(board_[6]));
    absl::StrAppend(&str, std::to_string(board_[7]));
    absl::StrAppend(&str, "  │");
    absl::StrAppend(&str, std::to_string(board_[10]));
    absl::StrAppend(&str, std::to_string(board_[11]));
    absl::StrAppend(&str, "│\n");
    absl::StrAppend(&str, "├──┬──┼──┤\n");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, std::to_string(board_[16]));
    absl::StrAppend(&str, std::to_string(board_[17]));
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, std::to_string(board_[14]));
    absl::StrAppend(&str, std::to_string(board_[15]));
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, std::to_string(board_[12]));
    absl::StrAppend(&str, std::to_string(board_[13]));
    absl::StrAppend(&str, "│\n");
    absl::StrAppend(&str, "└──┴──┴──┘\n");

    // absl::StrAppend(&str, "CW: "));
    absl::StrAppend(&str, std::to_string(current_wave_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(current_phase_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(num_moves_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(blue_hits_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(red_hits_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(blue_points_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(red_points_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(blue_placeable_fighters_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(red_placeable_fighters_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(red_placeable_sams_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(current_player_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(low_strike_attacks_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(max_low_strike_attacks_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(active_sam_attacks_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(max_active_sam_attacks_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(passive_sam_attacks_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(max_passive_sam_attacks_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(airbase_attacks_));
    absl::StrAppend(&str, " │");
    absl::StrAppend(&str, std::to_string(max_airbase_attacks_));
    absl::StrAppend(&str, "\n");
    absl::StrAppend(&str, "CW");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "CP");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "NM");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "BH");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "RH");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "BP");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "RP");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "BF");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "RF");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "RS");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "PL");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "LS");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "ML");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "AS");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "MA");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "PS");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "MP");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "AB");
    absl::StrAppend(&str, "│");
    absl::StrAppend(&str, "MA");
    return str;
}

bool CounterAirState::IsTerminal() const {
    return outcome_ != kInvalidPlayer || FinalRoundEnd();
}

std::vector<double> CounterAirState::Returns() const {
    if (outcome_ == 0) {
        return {1, -1};
    } else if (outcome_ == -1) {
        return {0, 0};
    } else {
        return {-1, 1};
    }
}

std::string CounterAirState::InformationStateString(Player player) const {
    SPIEL_CHECK_GE(player, 0);
    SPIEL_CHECK_LT(player, num_players_);
    return HistoryString();
}

std::string CounterAirState::ObservationString(Player player) const {
    SPIEL_CHECK_GE(player, 0);
    SPIEL_CHECK_LT(player, num_players_);
    return ToString();
}

void CounterAirState::ObservationTensor(Player player,
                                        absl::Span<float> values) const {
    SPIEL_CHECK_GE(player, 0);
    SPIEL_CHECK_LT(player, num_players_);

    // Treat `values` as a 2-d tensor.
    TensorView<1> view(values, {246}, true);
    for (int i = 0; i < 7; i++) {
        view[{static_cast<int>(i * 11 + board_[i])}] = 1.0;          // Blue fighters
        view[{static_cast<int>(87 + i * 5 + board_[8 + i])}] = 1.0;  // Red fighters/SAMs
    }
    view[{static_cast<int>(127 + board_[16])}] = 1.0;  // Attacking AAA
    view[{static_cast<int>(132 + board_[17])}] = 1.0;  // Evading AAA
    view[{static_cast<int>(137 + current_wave_)}] = 1.0;
    view[{static_cast<int>(142 + current_phase_)}] = 1.0;
    view[{static_cast<int>(153 + blue_hits_)}] = 1.0;
    view[{static_cast<int>(157 + red_hits_)}] = 1.0;
    view[{static_cast<int>(161 + blue_points_)}] = 1.0;
    view[{static_cast<int>(170 + red_points_)}] = 1.0;
    view[{static_cast<int>(181 + blue_placeable_fighters_)}] = 1.0;
    view[{static_cast<int>(192 + red_placeable_fighters_)}] = 1.0;
    view[{static_cast<int>(197 + red_placeable_sams_)}] = 1.0;
    view[{static_cast<int>(202 + attacking_box_)}] = 1.0;
    view[{static_cast<int>(210 + int(is_attacking_))}] = 1.0;
    view[{static_cast<int>(212 + current_player_)}] = 1.0;
    view[{static_cast<int>(214 + low_strike_attacks_)}] = 1.0;
    view[{static_cast<int>(218 + max_low_strike_attacks_)}] = 1.0;
    view[{static_cast<int>(222 + active_sam_attacks_)}] = 1.0;
    view[{static_cast<int>(226 + max_active_sam_attacks_)}] = 1.0;
    view[{static_cast<int>(230 + passive_sam_attacks_)}] = 1.0;
    view[{static_cast<int>(234 + max_passive_sam_attacks_)}] = 1.0;
    view[{static_cast<int>(238 + airbase_attacks_)}] = 1.0;
    view[{static_cast<int>(242 + max_airbase_attacks_)}] = 1.0;

    for (int i = 0; i < 246; i++) {
        // std::cout << std::to_string(view[{static_cast<int>(i)}]);
    }
}

void CounterAirState::UndoAction(Player player, Action move) {
    // current_player_ = player;
    // outcome_ = kInvalidPlayer;
    // num_moves_ -= 1;
    // history_.pop_back();
    // --move_number_;
}

std::unique_ptr<State> CounterAirState::Clone() const {
    return std::unique_ptr<State>(new CounterAirState(*this));
}

std::string CounterAirGame::ActionToString(Player player,
                                           Action action_id) const {
    return absl::StrCat(PlayerToString(player), "(",
                        action_id, ")");
}

CounterAirGame::CounterAirGame(const GameParameters &params)
    : Game(kGameType, params) {}

}  // namespace counter_air
}  // namespace open_spiel
