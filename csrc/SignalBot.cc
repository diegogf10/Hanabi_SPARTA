#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <map>
#include <iomanip>
#include "Hanabi.h"
#include "SignalBot.h"
#include "BotFactory.h"

using namespace Hanabi;
using namespace SignB;

// Register the bot with the factory
static void _registerBots() {
    registerBotFactory("SignalBot", std::shared_ptr<Hanabi::BotFactory>(new ::BotFactory<SignalBot>()));
}
static int dummy = (_registerBots(), 0);

// CardKnowledge implementation
CardKnowledge::CardKnowledge() : 
    isPlayable(false), 
    isDiscardable(false) {
    possibleColors.resize(NUMCOLORS, true);
    possibleValues.resize(5, true);
    hinted.value = 0;
    hinted.color = 0;
    hinted.composite = false;
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

SignalBot::SignalBot(int index, int numPlayers, int handSize): 
    me_(index), 
    numPlayers_(numPlayers) {
    // Initialize hand knowledge for all players
        handKnowledge_.resize(numPlayers);
        for (int i = 0; i < numPlayers; ++i) {
            handKnowledge_[i].resize(handSize);
        }
    }

SignB::SignalInterpretation SignalBot::interpretDirectSignal(
    const Server& server, int from, int to, const CardIndices& card_indices) 
{
    SignalInterpretation result{SignalType::DIRECT, 0.0, -1, false, false};
    
    // Check each hinted card for playability likelihood
    std::vector<std::pair<int, double>> cardScores;
    for (int i = 0; i < server.sizeOfHandOfPlayer(to); ++i) {
        if (!card_indices.contains(i)) continue;
        
        const auto& knowledge = handKnowledge_[to][i];
        double score = 0.0;

        // Base probability of being playable
        score += calculatePlayProbability(knowledge) * 0.5;
        
        // More likely to be playable if it's the only card hinted
        if (card_indices.size() == 1) {
            score += 0.2;
        }
        
        // Consider position - newer cards slightly more likely to be the target
        score += (i * 0.05);
        
        // Consider hint history
        if (knowledge.hinted.value > 0 || knowledge.hinted.color > 0) {
            score += 0.1;  // Previous hints increase likelihood
        }

        // Consider game state
        for (Color color = RED; color <= BLUE; ++color) {
            int nextValue = server.pileOf(color).size() + 1;
            if (nextValue <= 5) {  // Don't consider completed piles
                bool prerequisitesMet = true;
                // Check if all prerequisites for this value are available
                for (int v = server.pileOf(color).size() + 1; v < nextValue; ++v) {
                    Card prereq(color, v);
                    int inPlay = 0;
                    // Count copies still in play
                    for (int p = 0; p < server.numPlayers(); ++p) {
                        if (p != to) {  // Don't count target player's hand
                            for (const auto& card : server.handOfPlayer(p)) {
                                if (card == prereq) inPlay++;
                            }
                        }
                    }
                    if (inPlay == 0) {
                        prerequisitesMet = false;
                        break;
                    }
                }
                if (prerequisitesMet) {
                    score += 0.1;  // Card more likely to be playable if prerequisites are available
                }
            }
        }

        cardScores.push_back({i, score});
    }
    
    // Find the card with highest score
    auto bestCard = std::max_element(cardScores.begin(), cardScores.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    
    if (bestCard != cardScores.end() && bestCard->second >= 0.6) {
        result.cardIndex = bestCard->first;
        result.confidence = bestCard->second;
        result.isPlayable = (bestCard->second >= 0.8);
    }
    
    return result;
}

SignB::SignalInterpretation SignalBot::interpretIndirectSignal(
    const Server& server, int from, int to, const CardIndices& card_indices) 
{
    SignalInterpretation result{SignalType::INDIRECT, 0.0, -1, false, false};
    
    // Look for cards that were NOT indicated
    std::vector<int> unmentionedCards;
    for (int i = 0; i < server.sizeOfHandOfPlayer(to); i++) {
        if (!card_indices.contains(i)) {
            unmentionedCards.push_back(i);
        }
    }
    
    if (unmentionedCards.size() == 1) {
        result.cardIndex = unmentionedCards[0];
        result.confidence = 0.6;
        result.isValuable = true;  // Might be a warning about a valuable card
    }
    
    return result;
}

SignB::SignalInterpretation SignalBot::interpretCompositeSignal(
    const Server& server, int from, int to, const CardIndices& card_indices) 
{
    SignalInterpretation result{SignalType::COMPOSITE, 0.0, -1, false, false};
    
    auto it = recentHints_.rbegin();
    if (it != recentHints_.rend() && it->fromPlayer == from) {
        auto prevHint = std::next(it);
        if (prevHint != recentHints_.rend() && 
            prevHint->fromPlayer == from && 
            currentTurn_ - prevHint->turnReceived <= 2) {
            
            std::vector<int> commonCards;
            for (int idx : it->cardIndices) {
                if (std::find(prevHint->cardIndices.begin(), 
                            prevHint->cardIndices.end(), idx) 
                    != prevHint->cardIndices.end()) {
                    commonCards.push_back(idx);
                }
            }
            
            if (!commonCards.empty()) {
                result.cardIndex = commonCards[0];
                result.confidence = 0.9;
                
                if (it->isColor != prevHint->isColor) {
                    result.confidence = 0.95;
                    result.isPlayable = true;
                }
            }
        }
    }
    
    return result;
}

SignB::SignalInterpretation SignalBot::interpretContextualSignal(
    const Server& server, int from, int to, const CardIndices& card_indices) 
{
    SignalInterpretation result{SignalType::CONTEXTUAL, 0.0, -1, false, false};
    
    bool isEndgame = server.cardsRemainingInDeck() <= numPlayers_;
    bool isLowOnLives = server.mulligansRemaining() == 1;
    
    if (isEndgame) {
        for (int i = 0; i < server.sizeOfHandOfPlayer(to); i++) {
            if (card_indices.contains(i)) {
                const auto& knowledge = handKnowledge_[to][i];
                if (knowledge.hinted.value > 3) {
                    result.cardIndex = i;
                    result.confidence = 0.7;
                    result.isPlayable = true;
                    break;
                }
            }
        }
    }
    
    if (isLowOnLives) {
        result.confidence *= 0.8;
    }
    
    return result;
}

bool SignalBot::handleSignalPlay(Server& server) {
    std::vector<std::pair<int, double>> playConfidence;
    
    for (const auto& hint : recentHints_) {
        if (hint.turnReceived == currentTurn_ - 1 && hint.fromPlayer != me_) {
            CardIndices indices;
            for (int idx : hint.cardIndices) {
                indices.add(idx);
            }
            
            std::vector<SignalInterpretation> interpretations;
            interpretations.push_back(interpretDirectSignal(server, hint.fromPlayer, me_, indices));
            interpretations.push_back(interpretIndirectSignal(server, hint.fromPlayer, me_, indices));
            interpretations.push_back(interpretCompositeSignal(server, hint.fromPlayer, me_, indices));
            interpretations.push_back(interpretContextualSignal(server, hint.fromPlayer, me_, indices));
            
            auto bestInterpretation = std::max_element(
                interpretations.begin(), interpretations.end(),
                [](const auto& a, const auto& b) { return a.confidence < b.confidence; }
            );
            
            if (bestInterpretation != interpretations.end() && 
                bestInterpretation->cardIndex != -1) {
                playConfidence.push_back({
                    bestInterpretation->cardIndex,
                    bestInterpretation->confidence
                });
            }
        }
    }
    
    auto bestPlay = std::max_element(
        playConfidence.begin(), playConfidence.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; }
    );
    
    if (bestPlay != playConfidence.end() && bestPlay->second >= 0.9) {
        server.pleasePlay(bestPlay->first);
        return true;
    }
    
    return false;
}

bool SignalBot::handleSignalGive(Server& server) {
    struct HintOption {
        int player;
        bool isColor;
        int value;
        double utility;
        std::vector<int> affectedCards;
    };
    std::vector<HintOption> possibleHints;

    // Generate all possible hints
    for (int p = 0; p < numPlayers_; p++) {
        if (p == me_) continue;
        
        const auto& hand = server.handOfPlayer(p);
        const auto& knowledge = handKnowledge_[p];
        
        // Try color hints
        for (Color c = RED; c <= BLUE; ++c) {
            std::vector<int> affectedIndices;
            double utility = 0.0;
            
            // See what cards would be affected
            for (int i = 0; i < hand.size(); i++) {
                if (hand[i].color == c) {
                    affectedIndices.push_back(i);
                    
                    // Points for revealing playable cards
                    if (isCardPlayable(hand[i])) {
                        utility += 1.0;
                        // Extra points if this completes a composite signal
                        if (knowledge[i].hinted.value > 0) utility += 0.5;
                    }
                    
                    // Points for preventing discards of valuable cards
                    if (isCardCritical(hand[i])) utility += 0.3;
                    
                    // Penalty for misleading signals
                    if (!isCardPlayable(hand[i]) && affectedIndices.size() == 1) {
                        utility -= 0.4;
                    }
                }
            }
            
            if (!affectedIndices.empty()) {
                possibleHints.push_back({p, true, static_cast<int>(c), utility, affectedIndices});
            }
        }
        
        // Try value hints (similar structure)
        for (int v = 1; v <= 5; ++v) {
            std::vector<int> affectedIndices;
            double utility = 0.0;
            
            for (int i = 0; i < hand.size(); i++) {
                if (hand[i].value == v) {
                    affectedIndices.push_back(i);
                    
                    if (isCardPlayable(hand[i])) {
                        utility += 1.0;
                        if (knowledge[i].hinted.color > 0) utility += 0.5;
                    }
                    
                    if (isCardCritical(hand[i])) utility += 0.3;
                    
                    if (!isCardPlayable(hand[i]) && affectedIndices.size() == 1) {
                        utility -= 0.4;
                    }
                }
            }
            
            if (!affectedIndices.empty()) {
                possibleHints.push_back({p, false, v, utility, affectedIndices});
            }
        }
    }
    
    // Find the best hint
    auto bestHint = std::max_element(possibleHints.begin(), possibleHints.end(),
        [](const auto& a, const auto& b) { return a.utility < b.utility; });
        
    if (bestHint != possibleHints.end() && 
        bestHint->utility > 0.5 && 
        server.hintStonesRemaining() > 0) {
        
        if (bestHint->isColor) {
            server.pleaseGiveColorHint(bestHint->player, static_cast<Color>(bestHint->value));
        } else {
            server.pleaseGiveValueHint(bestHint->player, static_cast<Value>(bestHint->value));
        }
        return true;
    }
    
    return false;
}

bool SignalBot::tryPlaySafeCard(Hanabi::Server& server) {
    for (int i = 0; i < server.sizeOfHandOfPlayer(me_); i++) {
        const CardKnowledge& knowledge = handKnowledge_[me_][i];
        if (calculatePlayProbability(knowledge) >= 0.9) {
            server.pleasePlay(i);
            return true;
        }
    }
    return false;
}

bool SignalBot::tryDiscardSafeCard(Hanabi::Server& server) {
    if (!server.discardingIsAllowed()) return false;
    
    for (int i = 0; i < server.sizeOfHandOfPlayer(me_); i++) {
        const CardKnowledge& knowledge = handKnowledge_[me_][i];
        if (knowledge.isDiscardable) {
            server.pleaseDiscard(i);
            return true;
        }
    }
    return false;
}

// Utility methods
double SignalBot::calculatePlayProbability(const CardKnowledge& knowledge) const {
    if (knowledge.isPlayable) return 1.0;
    
    int playableCombs = 0;
    int totalCombs = 0;
    
    for (int c = 0; c < knowledge.possibleColors.size(); c++) {
        if (!knowledge.possibleColors[c]) continue;
        
        for (int v = 0; v < knowledge.possibleValues.size(); v++) {
            if (!knowledge.possibleValues[v]) continue;
            
            totalCombs++;
            if (server_->pileOf(static_cast<Color>(c)).nextValueIs(v + 1)) {
                playableCombs++;
            }
        }
    }
    
    return totalCombs > 0 ? static_cast<double>(playableCombs) / totalCombs : 0.0;
}

bool SignalBot::isCardPlayable(const Card& card) const {
    return server_->pileOf(card.color).nextValueIs(card.value);
}

bool SignalBot::isCardCritical(const Card& card) const {
    if (server_->pileOf(card.color).contains(card.value)) {
        return false;
    }
    
    int discardedCopies = 0;
    for (const auto& discarded : server_->discards()) {
        if (discarded == card) {
            discardedCopies++;
        }
    }
    
    return discardedCopies == card.count() - 1;
}

// Update playability of all known cards
void SignalBot::updateCardPlayability() {
    for (int p = 0; p < numPlayers_; p++) {
        if (p == me_) continue;
        
        const auto& hand = server_->handOfPlayer(p);
        for (int c = 0; c < hand.size(); c++) {
            CardKnowledge& knowledge = handKnowledge_[p][c];
            
            // Clear previous state
            knowledge.isPlayable = false;
            knowledge.isDiscardable = false;
            
            // If we can see the card (not our own), we can determine playability
            const Card& card = hand[c];
            
            // Check if card is playable
            knowledge.isPlayable = isCardPlayable(card);
            
            // Check if card is safely discardable
            knowledge.isDiscardable = !isCardCritical(card) || 
                server_->pileOf(card.color).contains(card.value);
            
            // Update composite signal info if we have complete knowledge
            if (knowledge.hinted.color > 0 && knowledge.hinted.value > 0) {
                knowledge.hinted.composite = true;
            }
        }
    }
}

void SignalBot::cleanupOldHints() {
    while (!recentHints_.empty() && 
           (currentTurn_ - recentHints_.front().turnReceived) > numPlayers_) {
        recentHints_.erase(recentHints_.begin());
    }
}


// Observation and move methods
void SignalBot::pleaseObserveBeforeMove(const Server& server) {
    server_ = &server;
    assert(server.whoAmI() == me_);
    
    // Update hand knowledge sizes
    for (int p = 0; p < numPlayers_; p++) {
        handKnowledge_[p].resize(server.sizeOfHandOfPlayer(p));
    }

    // Update information about all cards
    updateCardPlayability();
}

void SignalBot::pleaseMakeMove(Server& server) {
    currentTurn_++;
    cleanupOldHints();
    
    // Try to play based on received signals
    if (handleSignalPlay(server)) return;
    
    // Try to give signals about important cards
    if (handleSignalGive(server)) return;
    
    // Make safe plays if possible
    if (tryPlaySafeCard(server)) return;
    
    // Last resort - discard or give fallback hint
    if (server.discardingIsAllowed()) {
        if (tryDiscardSafeCard(server)) return;
        server.pleaseDiscard(0);
    } else {
        int nextPlayer = (me_ + 1) % numPlayers_;
        const auto& hand = server.handOfPlayer(nextPlayer);
        if (!hand.empty()) {
            server.pleaseGiveValueHint(nextPlayer, hand[0].value);
        }
    }
}

void SignalBot::pleaseObserveBeforeDiscard(const Hanabi::Server& server, int from, int card_index) {
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

void SignalBot::pleaseObserveBeforePlay(const Hanabi::Server& server, int from, int card_index) {
    pleaseObserveBeforeDiscard(server, from, card_index);
}

void SignalBot::pleaseObserveColorHint(const Server& server, int from, int to,
    Color color, CardIndices card_indices) 
{
    // Update card knowledge
    for (int i = 0; i < server.sizeOfHandOfPlayer(to); i++) {
        auto& knowledge = handKnowledge_[to][i];
        knowledge.updateFromHint(true, static_cast<int>(color), card_indices.contains(i));
        if (card_indices.contains(i)) {
            knowledge.hinted.color++;
        }
    }
    
    // Convert CardIndices to vector
    std::vector<int> indices;
    for (int i = 0; i < card_indices.size(); ++i) {
        indices.push_back(card_indices[i]);
    }
    
    // Record hint for pattern matching
    recentHints_.push_back({
        from,
        true,
        static_cast<int>(color),
        indices,
        currentTurn_
    });
}

void SignalBot::pleaseObserveValueHint(const Server& server, int from, int to,
    Value value, CardIndices card_indices) 
{
    // Update card knowledge
    for (int i = 0; i < server.sizeOfHandOfPlayer(to); i++) {
        auto& knowledge = handKnowledge_[to][i];
        knowledge.updateFromHint(false, static_cast<int>(value), card_indices.contains(i));
        if (card_indices.contains(i)) {
            knowledge.hinted.value++;
        }
    }
    
    // Convert CardIndices to vector
    std::vector<int> indices;
    for (int i = 0; i < card_indices.size(); ++i) {
        indices.push_back(card_indices[i]);
    }
    
    // Record hint for pattern matching
    recentHints_.push_back({
        from,
        false,
        static_cast<int>(value),
        indices,
        currentTurn_
    });
}

void SignalBot::pleaseObserveAfterMove(const Hanabi::Server& server) {
    assert(server.whoAmI() == me_);
}

SignalBot* SignalBot::clone() const {
    SignalBot* b = new SignalBot(me_, numPlayers_, handKnowledge_[0].size());
    
    // Copy basic state
    b->server_ = this->server_;
    b->currentTurn_ = this->currentTurn_;
    
    // Copy hand knowledge
    assert(this->handKnowledge_.size() == b->handKnowledge_.size());
    for (int i = 0; i < handKnowledge_.size(); i++) {
        b->handKnowledge_[i].clear();
        for (const auto& knol : handKnowledge_[i]) {
            b->handKnowledge_[i].push_back(knol);
        }
    }
    
    // Copy hint history
    b->recentHints_ = this->recentHints_;
    
    // Copy permissive flag from base Bot class
    b->permissive_ = this->permissive_;
    
    return b;
}


