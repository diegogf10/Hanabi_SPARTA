#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <map>
#include <set>
#include <iomanip>
#include "PileBot.h"
#include "BotFactory.h"
#include "Hanabi.h"

using namespace Hanabi;
using namespace PileB;

// Register the bot with the factory
static void _registerBots() {
    registerBotFactory("PileBot", std::shared_ptr<Hanabi::BotFactory>(new ::BotFactory<PileBot>()));
}
static int dummy = (_registerBots(), 0);

CardKnowledge::CardKnowledge()
    : isPlayable(false)
    , isDiscardable(false) {
    possibleColors.resize(NUMCOLORS, true);
    possibleValues.resize(5, true);
}

void CardKnowledge::updateFromHint(bool isColor, int value, bool positive) {
    if (isColor) {
        if (positive) {
            for (int c = 0; c < NUMCOLORS; c++) {
                possibleColors[c] = (c == value);
            }
        } else {
            possibleColors[value] = false;
        }
    } else {
        if (positive) {
            for (int v = 0; v < 5; v++) {
                possibleValues[v] = (v == value - 1);
            }
        } else {
            possibleValues[value - 1] = false;
        }
    }
}

PileBot::PileBot(int index, int numPlayers, int handSize)
    : me_(index)
    , numPlayers_(numPlayers)
    , currentTurn_(0) {
    handKnowledge_.resize(numPlayers);
    for (int i = 0; i < numPlayers; ++i) {
        handKnowledge_[i].resize(handSize);
    }
}

std::vector<PileStatus> PileBot::getPrioritizedPiles(const Hanabi::Server& server) const {
    std::vector<PileStatus> piles;
    
    // Gather information about all piles
    for (Color color = RED; color <= BLUE; ++color) {
        PileStatus status;
        status.color = color;
        status.height = server.pileOf(color).size();
        status.nextValueNeeded = status.height + 1;
        status.isActive = true;  // Will be updated based on sorting
        piles.push_back(status);
    }
    
    // Sort piles by height in descending order
    std::sort(piles.begin(), piles.end(),
        [](const PileStatus& a, const PileStatus& b) {
            return a.height > b.height;
        });
    
    // Mark only top piles as active
    if (!piles.empty()) {
        int maxHeight = piles[0].height;
        for (auto& pile : piles) {
            pile.isActive = (pile.height == maxHeight);
        }
    }
    
    return piles;
}

Color PileBot::getMostAdvancedPlayablePile(const Hanabi::Server& server) const {
    auto piles = getPrioritizedPiles(server);
    for (const auto& pile : piles) {
        if (pile.isActive) {
            return pile.color;
        }
    }
    return RED;  // Default fallback
}

bool PileBot::tryPlayPriorityCard(Hanabi::Server& server) {
    auto priorityPiles = getPrioritizedPiles(server);
    const double play_threshold = (server.hintStonesRemaining() <= 2) ? 0.5 : 0.6;

    // Try to play cards that advance priority piles
    for (const auto& pile : priorityPiles) {
        if (!pile.isActive) continue;
        
        for (int i = 0; i < server.sizeOfHandOfPlayer(me_); i++) {
            const CardKnowledge& knowledge = handKnowledge_[me_][i];
            if (calculatePlayProbability(knowledge, pile.color) > play_threshold) {
                server.pleasePlay(i);
                return true;
            }
        }
    }
    
    return false;
}

bool PileBot::tryGivePriorityHint(Hanabi::Server& server) {
    if (server.hintStonesRemaining() == 0) return false;

    std::vector<HintOption> possibleHints;
    auto priorityPiles = getPrioritizedPiles(server);
    
    // Check each player for cards that could advance any pile
    for (int p = 0; p < numPlayers_; p++) {
        if (p == me_) continue;
        
        const auto& hand = server.handOfPlayer(p);
        const auto& knowledge = handKnowledge_[p];
        
        // Track what information each card needs
        std::vector<bool> needsColorInfo(hand.size());
        std::vector<bool> needsValueInfo(hand.size());
        for (int i = 0; i < hand.size(); i++) {
            needsColorInfo[i] = std::count(knowledge[i].possibleColors.begin(), 
                                         knowledge[i].possibleColors.end(), true) > 1;
            needsValueInfo[i] = std::count(knowledge[i].possibleValues.begin(), 
                                         knowledge[i].possibleValues.end(), true) > 1;
        }

        // Try color hints for all piles
        for (const auto& pile : priorityPiles) {
            std::vector<int> colorAffected;
            std::vector<int> newlyInformed;
            bool hasPlayableCard = false;
            
            for (int i = 0; i < hand.size(); i++) {
                if (hand[i].color == pile.color) {
                    colorAffected.push_back(i);
                    if (needsColorInfo[i]) {
                        newlyInformed.push_back(i);
                    }
                    // Check if this card is playable
                    if (isCardPlayable(hand[i])) {
                        hasPlayableCard = true;
                    }
                }
            }
            
            // Consider hint if it provides new information
            if (!newlyInformed.empty()) {
                double value = evaluateHintValue(p, pile.color);
                
                // Bonus for completing card knowledge
                for (int idx : newlyInformed) {
                    if (!needsValueInfo[idx]) {
                        value *= 1.3;
                    }
                }

                // Extra bonus if this hint reveals a playable card
                if (hasPlayableCard) {
                    value *= 2.0;
                }

                possibleHints.push_back({
                    p, true, static_cast<int>(pile.color), 
                    value, colorAffected, newlyInformed
                });
            }
        }
        
        // Try value hints for all piles
        for (const auto& pile : priorityPiles) {            
            std::vector<int> valueAffected;
            std::vector<int> newlyInformed;
            bool hasPlayableCard = false;
            
            for (int i = 0; i < hand.size(); i++) {
                if (hand[i].value == pile.nextValueNeeded) {
                    valueAffected.push_back(i);
                    if (needsValueInfo[i]) {
                        newlyInformed.push_back(i);
                    }
                    // Check if this card is playable
                    if (isCardPlayable(hand[i])) {
                        hasPlayableCard = true;
                    }
                }
            }
            
            if (!newlyInformed.empty()) {
                double value = evaluateHintValue(p, static_cast<Value>(pile.nextValueNeeded));
                
                for (int idx : newlyInformed) {
                    if (!needsColorInfo[idx]) {
                        value *= 1.3;
                    }
                }

                // Extra bonus if this hint reveals a playable card
                if (hasPlayableCard) {
                    value *= 2.0;
                }

                possibleHints.push_back({
                    p, false, pile.nextValueNeeded,
                    value, valueAffected, newlyInformed
                });
            }
        }
    }
    
    // Find best hint
    double bestValue = 0.5;
    const HintOption* bestHint = nullptr;
    
    for (const auto& hint : possibleHints) {
        double value = hint.baseValue;
        
        // Keep preference for advanced piles but don't ignore others
        if (hint.useColor) {
            Color color = static_cast<Color>(hint.value);
            value *= (1.0 + server.pileOf(color).size() * 0.5);
        }
        
        if (value > bestValue) {
            bestValue = value;
            bestHint = &hint;
        }
    }
    
    if (bestHint != nullptr) {
        if (bestHint->useColor) {
            server.pleaseGiveColorHint(
                bestHint->targetPlayer, 
                static_cast<Color>(bestHint->value)
            );
        } else {
            server.pleaseGiveValueHint(
                bestHint->targetPlayer, 
                static_cast<Value>(bestHint->value)
            );
        }
        return true;
    }
    
    return false;
}

bool PileBot::trySafePriorityDiscard(Hanabi::Server& server) {
    if (!server.discardingIsAllowed()) return false;
    
    // Get current game state information
    auto priorityPiles = getPrioritizedPiles(server);
    const int handSize = server.sizeOfHandOfPlayer(me_);
    
    // First pass: Try to discard known unneeded cards
    for (int i = 0; i < handSize; i++) {
        const CardKnowledge& knowledge = handKnowledge_[me_][i];
        if (knowledge.isDiscardable) {
            server.pleaseDiscard(i);
            return true;
        }
    }
    
    // Second pass: Score each card for discard safety
    struct CardScore {
        int index;
        double safety_score;
    };
    std::vector<CardScore> cardScores;
    
    for (int i = 0; i < handSize; i++) {
        const CardKnowledge& knowledge = handKnowledge_[me_][i];

        // Skip recently hinted cards by giving them a very low safety score
        if (knowledge.lastHintTurn != -1 && 
            (currentTurn_ - knowledge.lastHintTurn) <= 1) {
            cardScores.push_back({i, -1.0});  // Very unsafe to discard
            continue;
        }

        double safety_score = 1.0;  // Base score
        
        // Reduce score for cards that might be critical
        int possibleCriticalColors = 0;
        for (int c = 0; c < NUMCOLORS; c++) {
            if (!knowledge.possibleColors[c]) continue;
            Card hypothetical(static_cast<Color>(c), 5); // Worst case: it's a 5
            if (isCardCritical(hypothetical)) {
                possibleCriticalColors++;
            }
        }
        if (possibleCriticalColors > 0) {
            safety_score *= (1.0 - (possibleCriticalColors / static_cast<double>(NUMCOLORS)));
        }
        
        // Reduce score for cards in priority piles
        for (const auto& pile : priorityPiles) {
            if (!pile.isActive) continue;
            double playProb = calculatePlayProbability(knowledge, pile.color);
            if (playProb > 0.2) {
                safety_score *= (1.0 - playProb);
            }
        }
        
        // Adjust score based on card position (prefer discarding newer cards)
        //safety_score *= (1.0 + (i * 0.1));
        
        cardScores.push_back({i, safety_score});
    }
    
    // If we're low on hint stones, we should discard more aggressively
    const double safety_threshold = (server.hintStonesRemaining() <= 2) ? 0.2 : 0.3;
    
    // Find the safest card to discard
    auto bestDiscard = std::max_element(
        cardScores.begin(), 
        cardScores.end(),
        [](const CardScore& a, const CardScore& b) {
            return a.safety_score < b.safety_score;
        }
    );
    
    if (bestDiscard != cardScores.end() && bestDiscard->safety_score > safety_threshold) {
        server.pleaseDiscard(bestDiscard->index);
        return true;
    }
    
    // If we absolutely must discard something, pick the least valuable card
    if (server.hintStonesRemaining() == 0) {
        // Find card with highest index (newest) that isn't definitely critical
        for (int i = handSize - 1; i >= 0; i--) {
            const CardKnowledge& knowledge = handKnowledge_[me_][i];
            bool mightBeCritical = false;
            
            for (int c = 0; c < NUMCOLORS; c++) {
                if (!knowledge.possibleColors[c]) continue;
                for (int v = 1; v <= 5; v++) {
                    if (!knowledge.possibleValues[v-1]) continue;
                    Card hypothetical(static_cast<Color>(c), v);
                    if (isCardCritical(hypothetical)) {
                        mightBeCritical = true;
                        break;
                    }
                }
                if (mightBeCritical) break;
            }
            
            if (!mightBeCritical) {
                server.pleaseDiscard(i);
                return true;
            }
        }
        // If everything might be critical, discard oldest card as last resort
        server.pleaseDiscard(handSize - 1);
        return true;
    }
    
    return false;  // Don't discard if we don't have to
}

double PileBot::calculatePlayProbability(const CardKnowledge& knowledge, Color targetColor) const {
    if (knowledge.isPlayable) return 1.0;
    
    int playableCombs = 0;
    int totalCombs = 0;
    
    const Pile& targetPile = server_->pileOf(targetColor);
    int pileHeight = targetPile.size();
    
    for (int c = 0; c < knowledge.possibleColors.size(); c++) {
        if (!knowledge.possibleColors[c]) continue;
        Color color = static_cast<Color>(c);
        const Pile& pile = server_->pileOf(color);
        
        for (int v = 0; v < knowledge.possibleValues.size(); v++) {
            if (!knowledge.possibleValues[v]) continue;
            
            totalCombs++;
            int value = v + 1;  // Convert to 1-based value
            
            // A card is only playable if:
            // 1. It's the next value needed
            // 2. All previous values are already played
            if (pile.nextValueIs(value)) {
                bool prerequisitesMet = true;
                for (int prev = 1; prev < value; prev++) {
                    if (!pile.contains(prev)) {
                        prerequisitesMet = false;
                        break;
                    }
                }
                if (prerequisitesMet) {
                    playableCombs++;
                }
            }
        }
    }
    
    return totalCombs > 0 ? static_cast<double>(playableCombs) / totalCombs : 0.0;
}

double PileBot::evaluateHintValue(int to, Color color) const {
    double value = 0.0;
    const auto& hand = server_->handOfPlayer(to);
    auto piles = getPrioritizedPiles(*server_);
    
    for (int i = 0; i < hand.size(); i++) {
        if (hand[i].color == color) {
            // Give extra points for ones that could start new piles
            if (hand[i].value == 1 && server_->pileOf(color).size() == 0) {
                value += 3.0;  // Higher bonus for starting new piles
                
                // Even more bonus if no other piles of this color are started
                bool isNewColor = true;
                for (const auto& pile : piles) {
                    if (pile.color == color && pile.height > 0) {
                        isNewColor = false;
                        break;
                    }
                }
                if (isNewColor) {
                    value += 1.0;  // Additional bonus for completely new colors
                }
            } else {
                // Original scoring for other cards
                for (const auto& pile : piles) {
                    if (pile.isActive && pile.color == color) {
                        if (hand[i].value == pile.nextValueNeeded) value += 2.0;
                        else if (hand[i].value > pile.nextValueNeeded) value += 0.5;
                    }
                }
            }
        }
    }
    
    return value;
}

double PileBot::evaluateHintValue(int to, Value value) const {
    double v = 0.0;
    const auto& hand = server_->handOfPlayer(to);
    auto piles = getPrioritizedPiles(*server_);
    
    if (value == 1) {
        // Special scoring for ones
        for (int i = 0; i < hand.size(); i++) {
            if (hand[i].value == 1) {
                // Check if this one could start a new pile
                if (server_->pileOf(hand[i].color).size() == 0) {
                    v += 3.0;  // Base bonus for a potential new pile
                    
                    // Extra bonus if early in the game
                    if (server_->cardsRemainingInDeck() > 30) {  // Adjust threshold as needed
                        v += 1.0;
                    }
                    
                    // Extra bonus if we have few active piles
                    int activePiles = 0;
                    for (const auto& pile : piles) {
                        if (pile.height > 0) activePiles++;
                    }
                    if (activePiles < 3) {  // Adjust threshold as needed
                        v += 1.0;
                    }
                }
            }
        }
    } else {
        // Original scoring for other values
        for (int i = 0; i < hand.size(); i++) {
            if (hand[i].value == value) {
                for (const auto& pile : piles) {
                    if (pile.isActive && value == pile.nextValueNeeded) {
                        v += 2.0;
                    }
                }
            }
        }
    }
    
    return v;
}

bool PileBot::isCardPlayable(const Card& card) const {
    return server_->pileOf(card.color).nextValueIs(card.value);
}

bool PileBot::isCardCritical(const Card& card) const {
    // A card is critical if:
    // 1. It hasn't been played yet
    // 2. All other copies have been discarded
    // 3. It's needed for one of the active piles
    
    if (server_->pileOf(card.color).contains(card.value)) {
        return false;  // Already played
    }
    
    int discardedCopies = 0;
    for (const auto& discarded : server_->discards()) {
        if (discarded == card) {
            discardedCopies++;
        }
    }
    
    // If this is the last copy and it's part of a priority pile, it's critical
    if (discardedCopies == card.count() - 1) {
        auto piles = getPrioritizedPiles(*server_);
        for (const auto& pile : piles) {
            if (pile.isActive && pile.color == card.color && card.value >= pile.nextValueNeeded) {
                return true;
            }
        }
    }
    
    return false;
}

bool PileBot::willCompletePile(const Card& card) const {
    // Check if playing this card would complete its pile
    return card.value == 5 && server_->pileOf(card.color).size() == 4;
}

bool PileBot::isPartOfPrioritizedPile(const Card& card) const {
    auto piles = getPrioritizedPiles(*server_);
    for (const auto& pile : piles) {
        if (pile.isActive && pile.color == card.color) {
            // Card is part of a priority pile if:
            // 1. It's the next needed card
            // 2. Or it's a higher card in the same pile that we'll need later
            return (card.value >= pile.nextValueNeeded);
        }
    }
    return false;
}

bool PileBot::shouldPreserveCard(const Card& card) const {
    // We should preserve a card if:
    // 1. It's part of a priority pile
    // 2. It's critical (last copy)
    // 3. It would complete a pile
    return isPartOfPrioritizedPile(card) || 
           isCardCritical(card) || 
           willCompletePile(card);
}

void PileBot::pleaseObserveBeforeMove(const Hanabi::Server& server) {
    server_ = &server;
    assert(server.whoAmI() == me_);
    
    // Update hand knowledge sizes
    for (int p = 0; p < numPlayers_; p++) {
        handKnowledge_[p].resize(server.sizeOfHandOfPlayer(p));
    }

    // Update playability and discardability for visible cards
    for (int p = 0; p < numPlayers_; p++) {
        if (p == me_) continue;
        
        const auto& hand = server.handOfPlayer(p);
        for (int c = 0; c < hand.size(); c++) {
            CardKnowledge& knowledge = handKnowledge_[p][c];
            knowledge.isPlayable = isCardPlayable(hand[c]);
            knowledge.isDiscardable = !isCardCritical(hand[c]);
        }
    }
}

void PileBot::pleaseMakeMove(Hanabi::Server& server) {
    server_ = &server;
    assert(server.whoAmI() == me_);
    currentTurn_++;
    
    // Try moves in priority order
    if (tryPlayPriorityCard(server)) return;
    if (tryGivePriorityHint(server)) return;
    if (trySafePriorityDiscard(server)) return;

    // If we have hint stones, give a hint to next player about their newest card
    // This is safer than playing or discarding blindly
    int nextPlayer = (me_ + 1) % numPlayers_;
    const auto& nextHand = server.handOfPlayer(nextPlayer);
    if (!nextHand.empty()) {
        // Prefer color hint if the card is playable
        if (isCardPlayable(nextHand.back())) {
            server.pleaseGiveColorHint(nextPlayer, nextHand.back().color);
        } else {
            // Otherwise give value hint
            server.pleaseGiveValueHint(nextPlayer, nextHand.back().value);
        }
        return;
    }
}

void PileBot::pleaseObserveBeforeDiscard(const Hanabi::Server& server, int from, int card_index) {
    auto& playerKnowledge = handKnowledge_[from];
    if (card_index < playerKnowledge.size() - 1) {
        for (int i = card_index; i < playerKnowledge.size() - 1; i++) {
            playerKnowledge[i] = playerKnowledge[i + 1];
        }
    }
    playerKnowledge.pop_back();
    
    if (server.cardsRemainingInDeck() > 0) {
        playerKnowledge.push_back(CardKnowledge());
    }
}

void PileBot::pleaseObserveBeforePlay(const Hanabi::Server& server, int from, int card_index) {
    pleaseObserveBeforeDiscard(server, from, card_index);
}

void PileBot::pleaseObserveColorHint(const Server& server, int from, int to, 
                                    Color color, CardIndices card_indices) {
    for (int i = 0; i < server.sizeOfHandOfPlayer(to); i++) {
        auto& knowledge = handKnowledge_[to][i];
        if (card_indices.contains(i)) {
            knowledge.lastHintTurn = currentTurn_;  // Record hint turn
        }
        knowledge.updateFromHint(true, static_cast<int>(color), 
                               card_indices.contains(i));
    }
}

void PileBot::pleaseObserveValueHint(const Server& server, int from, int to,
                                    Value value, CardIndices card_indices) {
    for (int i = 0; i < server.sizeOfHandOfPlayer(to); i++) {
        auto& knowledge = handKnowledge_[to][i];
        if (card_indices.contains(i)) {
            knowledge.lastHintTurn = currentTurn_;  // Record hint turn
        }
        knowledge.updateFromHint(false, static_cast<int>(value), 
                               card_indices.contains(i));
    }
}

void PileBot::pleaseObserveAfterMove(const Hanabi::Server& server) {
    assert(server.whoAmI() == me_);
}

PileBot* PileBot::clone() const {
    PileBot* b = new PileBot(me_, numPlayers_, handKnowledge_[0].size());

    b->currentTurn_ = this->currentTurn_;
    b->server_ = this->server_;
    
    assert(this->handKnowledge_.size() == b->handKnowledge_.size());
    for (int i = 0; i < handKnowledge_.size(); i++) {
        b->handKnowledge_[i].clear();
        for (const auto& knol : handKnowledge_[i]) {
            b->handKnowledge_[i].push_back(knol);
        }
    }
    
    b->permissive_ = this->permissive_;
    
    return b;
}