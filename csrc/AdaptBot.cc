#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <map>
#include <iomanip>
#include <set>
#include "AdaptBot.h"
#include "BotFactory.h"

using namespace Hanabi;
using namespace AdaB;

// Register the bot with the factory
static void _registerBots() {
    registerBotFactory("AdaptBot", std::shared_ptr<Hanabi::BotFactory>(new ::BotFactory<AdaptBot>()));
}
static int dummy = (_registerBots(), 0);

// CardKnowledge implementation
CardKnowledge::CardKnowledge()
    : isPlayable(false)
    , isDiscardable(false)
    , numHints(0)
    , playProbability(0.0)
    , criticalProbability(0.0) {
    possibleColors.resize(NUMCOLORS, true);
    possibleValues.resize(5, true);
}

void CardKnowledge::updateFromHint(bool isColor, int value, bool positive) {
    numHints++;
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

void CardKnowledge::updatePlayability(const Server& server) {
    int playableCombs = 0;
    int totalCombs = 0;
    int criticalCombs = 0;
    
    for (int c = 0; c < possibleColors.size(); c++) {
        if (!possibleColors[c]) continue;
        Color color = static_cast<Color>(c);
        
        for (int v = 0; v < possibleValues.size(); v++) {
            if (!possibleValues[v]) continue;
            
            totalCombs++;
            Card hypothetical(color, v + 1);
            
            if (server.pileOf(color).nextValueIs(v + 1)) {
                playableCombs++;
            }
            
            // Check if it could be a critical card
            int discardCount = 0;
            for (const auto& discard : server.discards()) {
                if (discard == hypothetical) discardCount++;
            }
            if (discardCount == hypothetical.count() - 1) {
                criticalCombs++;
            }
        }
    }
    
    playProbability = totalCombs > 0 ? static_cast<double>(playableCombs) / totalCombs : 0.0;
    if (playProbability == 1.0) {
        isPlayable = true;
    }
    criticalProbability = totalCombs > 0 ? static_cast<double>(criticalCombs) / totalCombs : 0.0;
}

AdaptBot::AdaptBot(int index, int numPlayers, int handSize)
    : me_(index)
    , partner_(1 - index)  // In 2-player game, partner is always the other player
    , playThreshold_(0.7)
    , hintThreshold_(0.5)
    , discardThreshold_(0.3) {
    
    // Initialize hand knowledge
    handKnowledge_.resize(numPlayers);
    for (int i = 0; i < numPlayers; ++i) {
        handKnowledge_[i].resize(handSize);
    }
    
    // Initialize player styles and histories
    playerStyles_.resize(numPlayers);
    moveHistory_.resize(numPlayers);
}

double AdaptBot::calculateRiskLevel(const Move& move) const {
    if (move.type == PLAY_CARD) {
        const auto& knowledge = handKnowledge_[me_][move.value];
        return 1.0 - knowledge.playProbability;
    }
    return 0.0;  // Non-play moves have no inherent risk
}

double AdaptBot::calculateHintEfficiency(const Move& move) const {
    if (move.type != HINT_COLOR && move.type != HINT_VALUE) return 0.0;
    
    // Calculate how many cards were affected by the hint
    int affectedCards = 0;
    const auto& partnerHand = server_->handOfPlayer(partner_);
    for (int i = 0; i < partnerHand.size(); i++) {
        if ((move.type == HINT_COLOR && partnerHand[i].color == static_cast<Color>(move.value)) ||
            (move.type == HINT_VALUE && partnerHand[i].value == move.value)) {
            affectedCards++;
        }
    }
    
    double efficiency = affectedCards / static_cast<double>(partnerHand.size());
    return efficiency;
}

void AdaptBot::updatePartnerStyle(const Server& server, const Move& move) {
    auto& style = playerStyles_[partner_];
    
    switch(move.type) {
        case PLAY_CARD: {
            double risk = calculateRiskLevel(move);
            style.riskTolerance = 0.9 * style.riskTolerance + 0.1 * risk;
            style.consecutiveHints = 0;
            style.consecutiveDiscards = 0;
            break;
        }
        case HINT_COLOR:
        case HINT_VALUE: {
            double efficiency = calculateHintEfficiency(move);
            style.hintEfficiency = 0.9 * style.hintEfficiency + 0.1 * efficiency;
            style.consecutiveHints++;
            style.consecutiveDiscards = 0;
            break;
        }
        case DISCARD_CARD: {
            bool hadHintStones = server.hintStonesRemaining() > 0;
            if (hadHintStones) {
                style.discardFrequency = 0.9 * style.discardFrequency + 0.1;
            }
            style.consecutiveDiscards++;
            style.consecutiveHints = 0;
            break;
        }
        default:
            break;
    }
    
    // Update dominant style based on metrics
    style.dominantStyle = determinePlayStyle(style);
}

PlayStyleType AdaptBot::determinePlayStyle(const PlayStyle& style) const {
    if (style.riskTolerance > 0.7) return PlayStyleType::AGGRESSIVE;
    if (style.hintEfficiency > 0.7) return PlayStyleType::HINT_FOCUSED;
    if (style.discardFrequency > 0.7) return PlayStyleType::DISCARD_FOCUSED;
    return PlayStyleType::CONSERVATIVE;
}

// Move analysis methods
AdaptBot::MoveAnalysis AdaptBot::analyzePotentialMove(const Server& server, const Move& move) const {
    MoveAnalysis analysis{0.0, 0.0, 0.0, 0.0};
    
    switch (move.type) {
        case PLAY_CARD: {
            const auto& knowledge = handKnowledge_[me_][move.value];
            analysis.risk = 1.0 - knowledge.playProbability;
            analysis.strategicValue = 2.0;  // Base value for playing cards
            if (knowledge.criticalProbability > 0.8) {
                analysis.risk *= 1.5;  // Increase risk if card might be critical
            }
            analysis.successProbability = knowledge.playProbability;
            break;
        }
        
        case HINT_COLOR:
        case HINT_VALUE: {
            analysis.informationGain = evaluateHintValue(move);
            // Higher value for hints that enable plays
            const auto& partnerHand = server.handOfPlayer(partner_);
            for (int i = 0; i < partnerHand.size(); i++) {
                if (server.pileOf(partnerHand[i].color).nextValueIs(partnerHand[i].value)) {
                    analysis.strategicValue += 1.0;
                }
            }
            analysis.successProbability = playerStyles_[partner_].hintEfficiency;
            break;
        }
        
        case DISCARD_CARD: {
            const auto& knowledge = handKnowledge_[me_][move.value];
            analysis.risk = knowledge.criticalProbability;
            analysis.strategicValue = server.hintStonesRemaining() < 8 ? 1.0 : 0.0;
            analysis.successProbability = 1.0 - knowledge.criticalProbability;
            break;
        }
        
        default:
            break;
    }
    
    return analysis;
}

double AdaptBot::calculateMoveScore(const MoveAnalysis& analysis, const PlayStyle& style) const {
    double score = 0.0;
    
    // Base score from success probability
    score += analysis.successProbability * 2.0;
    
    // Adjust for risk based on partner's style
    double riskAdjustment = style.riskTolerance > 0.6 ? 0.8 : 1.2;
    score -= analysis.risk * riskAdjustment;
    
    // Add strategic value
    score += analysis.strategicValue;
    
    // Add information value weighted by hint efficiency
    score += analysis.informationGain * style.hintEfficiency;
    
    return score;
}

// Core strategy methods
bool AdaptBot::tryPlayCard(Server& server) {
    std::vector<std::pair<int, double>> playableCards;
    
    // Evaluate all cards in hand
    for (int i = 0; i < server.sizeOfHandOfPlayer(me_); i++) {
        const auto& knowledge = handKnowledge_[me_][i];
        if (knowledge.playProbability >= playThreshold_ || knowledge.isPlayable) {
            playableCards.emplace_back(i, knowledge.playProbability);
        }
    }
    
    // Sort by play probability
    std::sort(playableCards.begin(), playableCards.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    if (!playableCards.empty()) {
        server.pleasePlay(playableCards[0].first);
        return true;
    }
    
    return false;
}

bool AdaptBot::tryGiveHint(Server& server) {
    // Wheb no hint stones are left, can't give hints
    if (server.hintStonesRemaining() == 0) return false;

    // Bot convention: give a color hint about a playable card OR a value hint about a non critical card that could be discarded
    if (isOneHintStoneLeft()) {
        return tryGiveOneStoneHint(server);
    }
    
    const auto& partnerHand = server.handOfPlayer(partner_);
    std::vector<Move> possibleHints;

    // Generate all possible hints
    for (Color color = RED; color <= BLUE; ++color) {
        bool colorPresent = false;
        for (const auto& card : partnerHand) {
            if (card.color == color) {
                colorPresent = true;
                break;
            }
        }
        if (colorPresent) {
            possibleHints.push_back(Move(HINT_COLOR, static_cast<int>(color), partner_));
        }
    }
    
    for (int value = 1; value <= 5; ++value) {
        bool valuePresent = false;
        for (const auto& card : partnerHand) {
            if (card.value == value) {
                valuePresent = true;
                break;
            }
        }
        if (valuePresent) {
            possibleHints.push_back(Move(HINT_VALUE, value, partner_));
        }
    }
    
    // Evaluate each hint
    Move bestHint;
    double bestValue = hintThreshold_;
    
    for (const auto& hint : possibleHints) {
        double value = evaluateHintValue(hint);
        if (value > bestValue) {
            bestValue = value;
            bestHint = hint;
        }
    }
    
    if (bestValue > hintThreshold_) {
        if (bestHint.type == HINT_COLOR) {
            server.pleaseGiveColorHint(bestHint.to, static_cast<Color>(bestHint.value));
        } else {
            server.pleaseGiveValueHint(bestHint.to, static_cast<Value>(bestHint.value));
        }
        return true;
    }
    
    return false;
}

bool AdaptBot::tryGiveOneStoneHint(Server& server) {
    const auto& partnerHand = server.handOfPlayer(partner_);
    
    // First priority: Look for playable cards
    // Check each color
    for (Color color = RED; color <= BLUE; ++color) {
        // Track which cards would be affected by this color hint
        std::vector<int> affectedIndices;
        bool hasPlayableCard = false;
        
        // Check cards from newest to oldest
        for (int i = partnerHand.size() - 1; i >= 0; i--) {
            const Card& card = partnerHand[i];
            if (card.color == color) {
                affectedIndices.push_back(i);
                // If this is the newest affected card, check if it's playable
                if (affectedIndices.size() == 1 && 
                    server.pileOf(card.color).nextValueIs(card.value)) {
                    hasPlayableCard = true;
                    break;  // We found our playable card, no need to check older ones
                }
            }
        }

        // If we found cards of this color and the newest one is playable, give the hint
        if (!affectedIndices.empty() && hasPlayableCard) {
            server.pleaseGiveColorHint(partner_, color);
            return true;
        }
    }

    // Second priority: Look for safely discardable cards
    for (int value = 1; value <= 5; ++value) {
        // Count how many cards of this value would be affected
        int affectedCards = 0;
        int discardableCardIndex = -1;
        bool hasCriticalCards = false;
        
        for (int i = 0; i < partnerHand.size(); i++) {
            const Card& card = partnerHand[i];
            if (card.value == value) {
                affectedCards++;
                if (!isCardCritical(card) && !(server_->pileOf(card.color).nextValueIs(card.value))) {
                    discardableCardIndex = i;
                } else {
                    hasCriticalCards = true;
                }
            }
        }
        
        // Only give value hint if it uniquely identifies a discardable card
        // or affects only the target card
        if (discardableCardIndex != -1 && !hasCriticalCards && affectedCards == 1) {
            server.pleaseGiveValueHint(partner_, static_cast<Value>(value));
            return true;
        }
    }
    
    return false;
}

bool AdaptBot::tryDiscard(Server& server) {
    if (!server.discardingIsAllowed()) return false;
    
    // Find safest card to discard
    int bestIndex = -1;
    double lowestRisk = 1.0;
    
    for (int i = 0; i < server.sizeOfHandOfPlayer(me_); i++) {
        const auto& knowledge = handKnowledge_[me_][i];
        double risk = knowledge.criticalProbability;
        
        if (risk < lowestRisk && risk < discardThreshold_) {
            lowestRisk = risk;
            bestIndex = i;
        }
    }
    
    if (bestIndex != -1) {
        server.pleaseDiscard(bestIndex);
        return true;
    }
    
    return false;
}

// Utility methods
double AdaptBot::evaluateHintValue(const Move& hint) const {
    double value = 0.0;
    const auto& partnerHand = server_->handOfPlayer(partner_);
    bool providesNewInfo = false;
    int numAffectedCards = 0;

    for (int i = 0; i < partnerHand.size(); i++) {
        const auto& card = partnerHand[i];
        const auto& knowledge = handKnowledge_[partner_][i];
        
        bool affected = false;
        if (hint.type == HINT_COLOR && card.color == static_cast<Color>(hint.value)) {
            // Check if this color info is actually new
            if (std::count(knowledge.possibleColors.begin(), 
                          knowledge.possibleColors.end(), true) > 1) {
                providesNewInfo = true;
            }
            affected = true;
            numAffectedCards++;
        } else if (hint.type == HINT_VALUE && card.value == hint.value) {
            // Check if this value info is actually new
            if (std::count(knowledge.possibleValues.begin(), 
                          knowledge.possibleValues.end(), true) > 1) {
                providesNewInfo = true;
            }
            affected = true;
            numAffectedCards++;
        }
        
        if (affected) {
            double cardValue = 1.0;
            bool isPlayable = server_->pileOf(card.color).nextValueIs(card.value);

            // Only give value if this provides new information or completes knowledge
            bool completesKnowledge = false;
            if (hint.type == HINT_COLOR) {
                completesKnowledge = std::count(knowledge.possibleValues.begin(), 
                                              knowledge.possibleValues.end(), 
                                              true) == 1;
            } else {
                completesKnowledge = std::count(knowledge.possibleColors.begin(), 
                                              knowledge.possibleColors.end(), 
                                              true) == 1;
            }

            if (!providesNewInfo && !completesKnowledge) {
                cardValue = 0.0;  // No value for redundant hints
            } else {
                if (isPlayable) {
                    cardValue *= 3.0;
                    if (completesKnowledge) {
                        cardValue *= 1.5;
                    }
                }
            }
            
            value += cardValue;
        }
    }

    // Return 0 if hint provides no new information at all
    if (!providesNewInfo) {
        return 0.0;
    }

    // Only apply multi-card bonus if the hint actually provides new info
    if (numAffectedCards > 1 && providesNewInfo) {
        value *= (1.0 + (numAffectedCards - 1) * 0.2);
    }

    return value;
}

void AdaptBot::pleaseObserveBeforeMove(const Server& server) {
    server_ = &server;
    assert(server.whoAmI() == me_);
    
    // Update hand knowledge sizes
    for (int p = 0; p < 2; p++) {  // Only 2 players
        handKnowledge_[p].resize(server.sizeOfHandOfPlayer(p));
    }

    // Update card knowledge
    updateCardKnowledge();
    
    // Adapt thresholds based on game state and partner's style
    adaptThresholds();
}

void AdaptBot::adaptThresholds() {
    const auto& partnerStyle = playerStyles_[partner_];
    
    // Adjust play threshold based on partner's style and game state
    if (partnerStyle.dominantStyle == PlayStyleType::AGGRESSIVE) {
        playThreshold_ = 0.7;  // We can be more conservative
    } else if (partnerStyle.dominantStyle == PlayStyleType::CONSERVATIVE) {
        playThreshold_ = 0.5;  // We need to be more aggressive
    }
    
    // Adjust hint threshold based on partner's hint efficiency
    hintThreshold_ = 0.4 + (0.3 * partnerStyle.hintEfficiency);
    
    // Adjust discard threshold based on partner's discard frequency
    discardThreshold_ = 0.3 + (0.2 * partnerStyle.discardFrequency);
    
    // Emergency adjustments if low on lives
    if (server_->mulligansRemaining() == 1) {
        playThreshold_ += 0.2;
        discardThreshold_ -= 0.1;
    }
}

std::vector<Move> AdaptBot::generatePossibleMoves(const Hanabi::Server& server) const {
    std::vector<Move> possibleMoves;
    
    // Add all possible plays
    for (int i = 0; i < server.sizeOfHandOfPlayer(me_); i++) {
        possibleMoves.emplace_back(PLAY_CARD, i);
    }
    
    // Add hints if we have hint stones
    if (server.hintStonesRemaining() > 0) {
        const auto& partnerHand = server.handOfPlayer(partner_);
        
        // Track which colors and values are present
        std::set<Color> presentColors;
        std::set<int> presentValues;
        
        for (const auto& card : partnerHand) {
            presentColors.insert(card.color);
            presentValues.insert(card.value);
        }
        
        // Add color hints
        for (Color color : presentColors) {
            possibleMoves.emplace_back(HINT_COLOR, static_cast<int>(color), partner_);
        }
        
        // Add value hints
        for (int value : presentValues) {
            possibleMoves.emplace_back(HINT_VALUE, value, partner_);
        }
    }
    
    // Add discards if allowed
    if (server.discardingIsAllowed()) {
        for (int i = 0; i < server.sizeOfHandOfPlayer(me_); i++) {
            possibleMoves.emplace_back(DISCARD_CARD, i);
        }
    }
    
    return possibleMoves;
}

double AdaptBot::calculatePlayProbability(const AdaB::CardKnowledge& knowledge) const {
    int playableCombs = 0;
    int totalCombs = 0;
    
    // First, get a count of all visible cards
    std::map<Card, int> visibleCards;
    
    // Add discarded cards
    for (const auto& card : server_->discards()) {
        visibleCards[card]++;
    }
    
    // Add cards in other players' hands
    for (int p = 0; p < server_->numPlayers(); p++) {
        if (p == me_) continue;  // Skip our own hand
        for (const Card& card : server_->handOfPlayer(p)) {
            visibleCards[card]++;
        }
    }
    
    // Add played cards (cards in fireworks)
    for (Color color = RED; color <= BLUE; ++color) {
        const Pile& pile = server_->pileOf(color);
        for (int value = 1; value <= pile.size(); ++value) {
            visibleCards[Card(color, value)]++;
        }
    }

    // Now evaluate each possible combination
    for (int c = 0; c < knowledge.possibleColors.size(); c++) {
        if (!knowledge.possibleColors[c]) continue;
        Color color = static_cast<Color>(c);
        
        for (int v = 0; v < knowledge.possibleValues.size(); v++) {
            if (!knowledge.possibleValues[v]) continue;
            
            Card potentialCard(color, v + 1);
            
            // Check if this combination is even possible given visible cards
            int visibleCount = visibleCards[potentialCard];
            if (visibleCount >= potentialCard.count()) {
                continue;  // All copies of this card are visible elsewhere
            }
            
            totalCombs++;
            
            // Check if card would be playable
            if (server_->pileOf(color).nextValueIs(v + 1)) {
                // Check if prerequisites are available
                bool prerequisitesAvailable = true;
                for (int prereqValue = 1; prereqValue < v + 1; prereqValue++) {
                    Card prereq(color, prereqValue);
                    if (!server_->pileOf(color).contains(prereqValue) && 
                        visibleCards[prereq] == prereq.count()) {
                        // A prerequisite card is completely discarded
                        prerequisitesAvailable = false;
                        break;
                    }
                }
                
                if (prerequisitesAvailable) {
                    playableCombs++;
                }
            }
        }
    }
    
    return totalCombs > 0 ? static_cast<double>(playableCombs) / totalCombs : 0.0;
}

bool AdaptBot::isCardCritical(const Hanabi::Card& card) const {
    // A card is critical if:
    // 1. It hasn't been played yet
    // 2. It's the last copy available
    if (server_->pileOf(card.color).contains(card.value)) {
        return false;
    }
    
    int discardedCopies = 0;
    for (const auto& discard : server_->discards()) {
        if (discard == card) {
            discardedCopies++;
        }
    }
    
    // Check if this is the last copy
    return discardedCopies == card.count() - 1;
}

void AdaptBot::analyzeHintPatterns() {
    auto& partnerHistory = moveHistory_[partner_];
    
    // Look for patterns in recent hints
    std::vector<Move> recentHints;
    for (const auto& move : partnerHistory.recentMoves) {
        if (move.type == HINT_COLOR || move.type == HINT_VALUE) {
            recentHints.push_back(move);
        }
    }
    
    // Analyze hint sequences
    if (recentHints.size() >= 2) {
        bool hasDoubleHint = false;
        for (size_t i = 1; i < recentHints.size(); i++) {
            if (recentHints[i].to == recentHints[i-1].to) {
                hasDoubleHint = true;
                // Partner might be trying to emphasize something
                // Adjust hint threshold for this player
                hintThreshold_ *= 0.9;
                break;
            }
        }
        
        if (!hasDoubleHint) {
            // Partner spreads hints - might be playing more conservatively
            hintThreshold_ *= 1.1;
        }
    }
}

void AdaptBot::updateHintEfficiency(const Move& move, bool resultedInPlay) {
    if (move.type != HINT_COLOR && move.type != HINT_VALUE) return;
    
    auto& style = playerStyles_[move.to];
    double efficiency = resultedInPlay ? 1.0 : 0.0;
    
    // Update hint efficiency with decay
    style.hintEfficiency = 0.8 * style.hintEfficiency + 0.2 * efficiency;
    
    // Update hint patterns
    HintPattern pattern{
        move.type == HINT_COLOR,
        move.value,
        -1,  // Will be set if we can determine which card was played
        resultedInPlay,
        0.0  // Will be updated when we observe the play
    };
    
    moveHistory_[move.to].hintPatterns.push_back(pattern);
}

double AdaptBot::predictMoveSuccess(const Move& move) const {
    switch (move.type) {
        case PLAY_CARD: {
            return handKnowledge_[me_][move.value].playProbability;
        }
        
        case HINT_COLOR:
        case HINT_VALUE: {
            // Base success on partner's historical hint efficiency
            return playerStyles_[partner_].hintEfficiency;
        }
        
        case DISCARD_CARD: {
            // Success is inverse of probability that card is critical
            return 1.0 - handKnowledge_[me_][move.value].criticalProbability;
        }
        
        default:
            return 0.0;
    }
}

void AdaptBot::pleaseMakeMove(Server& server) {
    server_ = &server;
    assert(server.whoAmI() == me_);

    // Try moves in priority order
    if (tryPlayCard(server)) {
        moveHistory_[me_].addMove(Move(PLAY_CARD, 0));
        return;
    }
    
    if (tryGiveHint(server)) {
        moveHistory_[me_].addMove(Move(HINT_COLOR, 0, partner_));
        return;
    }
    
    if (tryDiscard(server)) {
        moveHistory_[me_].addMove(Move(DISCARD_CARD, 0));
        return;
    }

    // If no preferred move found, make safest discard or give any hint
    if (server.discardingIsAllowed()) {
        server.pleaseDiscard(0);  // Discard oldest card
        moveHistory_[me_].addMove(Move(DISCARD_CARD, 0));
    } else {
        // Give hint about partner's newest card
        const auto& partnerHand = server.handOfPlayer(partner_);
        if (!partnerHand.empty()) {
            server.pleaseGiveColorHint(partner_, partnerHand.back().color);
            moveHistory_[me_].addMove(Move(HINT_COLOR, 0, partner_));
        }
    }
}

void AdaptBot::pleaseObserveBeforeDiscard(const Server& server, int from, int card_index) {
    // Update knowledge tracking
    auto& playerKnowledge = handKnowledge_[from];
    if (card_index < playerKnowledge.size() - 1) {
        for (int i = card_index; i < playerKnowledge.size() - 1; i++) {
            playerKnowledge[i] = playerKnowledge[i + 1];
        }
    }
    playerKnowledge.pop_back();
    
    // Add new blank knowledge if cards remain in deck
    if (server.cardsRemainingInDeck() > 0) {
        playerKnowledge.push_back(CardKnowledge());
    }

    // Update move history and analyze pattern
    if (from == partner_) {
        moveHistory_[from].addMove(Move(DISCARD_CARD, card_index));
        updatePartnerStyle(server, Move(DISCARD_CARD, card_index));
    }
}

void AdaptBot::pleaseObserveBeforePlay(const Server& server, int from, int card_index) {
    // Similar to discard but with play move
    pleaseObserveBeforeDiscard(server, from, card_index);
    
    if (from == partner_) {
        moveHistory_[from].addMove(Move(PLAY_CARD, card_index));
        updatePartnerStyle(server, Move(PLAY_CARD, card_index));
    }
}

void AdaptBot::pleaseObserveColorHint(const Server& server, int from, int to, 
                                      Color color, CardIndices card_indices) {
    // Update knowledge for each affected card
    for (int i = 0; i < server.sizeOfHandOfPlayer(to); i++) {
        handKnowledge_[to][i].updateFromHint(true, static_cast<int>(color), 
                                           card_indices.contains(i));
    }

   // Handle one-stone convention
    if (isOneHintStoneLeft() && to == me_) {
        // Find the newest affected card
        int newestAffectedIndex = -1;
        for (int i = server.sizeOfHandOfPlayer(me_) - 1; i >= 0; i--) {
            if (card_indices.contains(i)) {
                newestAffectedIndex = i;
                break;
            }
        }
        
        // If we found an affected card, it must be playable
        if (newestAffectedIndex != -1) {
            auto& knowledge = handKnowledge_[me_][newestAffectedIndex];
            knowledge.isPlayable = true;
        }
    }

    // Track hint pattern
    if (from == partner_) {
        Move hintMove(HINT_COLOR, static_cast<int>(color), to);
        moveHistory_[from].addMove(hintMove);
        updatePartnerStyle(server, hintMove);
    }
}

void AdaptBot::pleaseObserveValueHint(const Server& server, int from, int to,
                                      Value value, CardIndices card_indices) {
    // Update knowledge for each affected card
    for (int i = 0; i < server.sizeOfHandOfPlayer(to); i++) {
        handKnowledge_[to][i].updateFromHint(false, static_cast<int>(value), 
                                           card_indices.contains(i));
    }

    // Apply one-stone convention
    if (isOneHintStoneLeft() && to == me_) {
        int affectedCards = 0;
        int affectedIndex = -1;
        
        // Count affected cards
        for (int i = 0; i < server.sizeOfHandOfPlayer(me_); i++) {
            if (card_indices.contains(i)) {
                affectedCards++;
                affectedIndex = i;
            }
        }
        
        // Only interpret as discard hint if exactly one card is affected
        if (affectedCards == 1) {
            handKnowledge_[me_][affectedIndex].isDiscardable = true;
        }
    }

    // Track hint pattern
    if (from == partner_) {
        Move hintMove(HINT_VALUE, static_cast<int>(value), to);
        moveHistory_[from].addMove(hintMove);
        updatePartnerStyle(server, hintMove);
    }
}

void AdaptBot::pleaseObserveAfterMove(const Server& server) {
    assert(server.whoAmI() == me_);
    // Update card knowledge after move resolution
    updateCardKnowledge();
}

void AdaptBot::updateCardKnowledge() {
    // Update knowledge of visible cards
    for (int p = 0; p < 2; p++) {  // Only 2 players
        if (p == me_) continue;
        
        const auto& hand = server_->handOfPlayer(p);
        for (int c = 0; c < hand.size(); c++) {
            handKnowledge_[p][c].updatePlayability(*server_);
        }
    }
    
    // Update knowledge of our own cards
    for (auto& knowledge : handKnowledge_[me_]) {
        knowledge.updatePlayability(*server_);
    }
}

AdaptBot* AdaptBot::clone() const {
    AdaptBot* b = new AdaptBot(me_, 2, handKnowledge_[0].size());
    
    // Copy basic state
    b->server_ = this->server_;
    b->partner_ = this->partner_;
    b->playThreshold_ = this->playThreshold_;
    b->hintThreshold_ = this->hintThreshold_;
    b->discardThreshold_ = this->discardThreshold_;
    
    // Copy hand knowledge
    assert(this->handKnowledge_.size() == b->handKnowledge_.size());
    for (int i = 0; i < handKnowledge_.size(); i++) {
        b->handKnowledge_[i].clear();
        for (const auto& knol : handKnowledge_[i]) {
            b->handKnowledge_[i].push_back(knol);
        }
    }
    
    // Copy player styles and move histories
    b->playerStyles_ = this->playerStyles_;
    b->moveHistory_ = this->moveHistory_;
    
    // Copy permissive flag from base Bot class
    b->permissive_ = this->permissive_;
    
    return b;
}