#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <map>
#include <iomanip>
#include "Hanabi.h"
#include "MetaBot.h"
#include "BotFactory.h"

using namespace Hanabi;
using namespace MetaB;

// Register the bot with the factory
static void _registerBots() {
    registerBotFactory("MetaBot", std::shared_ptr<Hanabi::BotFactory>(new ::BotFactory<MetaBot>()));
}
static int dummy = (_registerBots(), 0);

CardKnowledge::CardKnowledge()
{
    // Initialize all colors as possible
    possibleColors.resize(NUMCOLORS, true);
    // Initialize all values as possible (values 1-5)
    possibleValues.resize(5, true);
    isPlayable = false;
    isDiscardable = false;
}

void CardKnowledge::updateFromHint(bool isColor, int value, bool positive) {
    if (isColor) {
        // If this is a color hint
        Color hintedColor = static_cast<Color>(value);
        
        if (positive) {
            // This card is of the hinted color
            // Set all other colors to false
            for (int c = 0; c < NUMCOLORS; c++) {
                possibleColors[c] = (c == value);
            }
        } else {
            // This card is not of the hinted color
            possibleColors[value] = false;
        }
    } else {
        // If this is a value hint
        if (positive) {
            // This card has the hinted value
            // Set all other values to false
            // Note: value parameter is 1-5, but vector is 0-based
            for (int v = 0; v < 5; v++) {
                possibleValues[v] = (v == value - 1);
            }
        } else {
            // This card does not have the hinted value
            possibleValues[value - 1] = false;
        }
    }
    
    // After updating knowledge, check if we can determine playability
    // A card is potentially playable if it could complete the next firework
    // This would need access to the game state though, so we'll manage this
    // through the main bot logic instead of here
}


// Game phase determination logic
GamePhase MetaBot::determineGamePhase(const Server& server) {
    auto metrics = calculateGameMetrics(server);
    
    // Check for crisis conditions first
    if (isCrisisPhase(metrics)) {
        return GamePhase::CRISIS;
    }
    
    // Determine normal game phases based on deck depletion
    if (metrics.deckDepletion < 0.33) {
        return GamePhase::OPENING;
    } else if (metrics.deckDepletion < 0.66) {
        return GamePhase::MIDGAME;
    } else {
        return GamePhase::ENDGAME;
    }
}

GameMetrics MetaBot::calculateGameMetrics(const Server& server) {
    GameMetrics metrics;
    
    // Calculate deck depletion
    const int totalCards = 50; // Total cards in a standard Hanabi deck
    metrics.deckDepletion = 1.0 - (server.cardsRemainingInDeck() / static_cast<double>(totalCards));
    
    // Calculate firework progress
    int totalProgress = 0;
    for (Color color = RED; color <= BLUE; ++color) {
        totalProgress += server.pileOf(color).size();
    }
    metrics.fireworkProgress = totalProgress / 25.0; // 25 is max possible progress
    
    // Calculate hint availability
    metrics.hintAvailability = server.hintStonesRemaining() / 8.0;
    
    // Calculate life buffer
    metrics.lifeBuffer = server.mulligansRemaining() / 3.0;
    
    // Calculate known cards ratio
    int knownCount = 0;
    for (const auto& playerHand : handKnowledge_) {
        for (const auto& card : playerHand) {
            if (std::count(card.possibleColors.begin(), card.possibleColors.end(), true) == 1 && std::count(card.possibleValues.begin(), card.possibleValues.end(), true) == 1) {
                knownCount++;
            }
        }
    }
    metrics.knownCards = knownCount / static_cast<double>(totalCards);
    
    return metrics;
}

// Constructor and basic setup
MetaBot::MetaBot(int index, int numPlayers, int handSize) 
    : me_(index), numPlayers_(numPlayers), currentPhase_(GamePhase::OPENING) {
    // Initialize hand knowledge for all players
    handKnowledge_.resize(numPlayers);
    for (int i = 0; i < numPlayers; ++i) {
        handKnowledge_[i].resize(handSize);
    }
}

bool MetaBot::isCrisisPhase(const GameMetrics& metrics) const {
    // Define crisis conditions
    return (metrics.lifeBuffer <= 0.34) ||                    // Only 1 life remaining
           (metrics.hintAvailability == 0.0) ||               // No hints available
           (metrics.deckDepletion > 0.9 && metrics.fireworkProgress < 0.6);  // Near end with poor progress
}

bool MetaBot::tryPlaySafeCard(Server& server) {
    // Go through each card in our hand
    for (int i = 0; i < server.sizeOfHandOfPlayer(me_); i++) {
        const CardKnowledge& knowledge = handKnowledge_[me_][i];
        
        // More aggresive plays as the game progresses unless it's crisis phase. Endgame has a specific function for risky plays
        if (calculatePlayProbability(knowledge) >= 0.95 || (currentPhase_ == GamePhase::MIDGAME && calculatePlayProbability(knowledge) >= 0.8)) {
            // Check if the card would contribute to a firework
            server.pleasePlay(i);
            return true;
        }
    }
    return false;
}

bool MetaBot::tryGiveInformativeHint(Server& server) {
    const auto& partnerKnowledge = handKnowledge_[(me_ + 1) % numPlayers_];
    std::vector<HintValue> possibleHints;
    const auto& partnerHand = server.handOfPlayer((me_ + 1) % numPlayers_);
    
    for (int i = 0; i < partnerHand.size(); i++) {
        const Card& card = partnerHand[i];
        const CardKnowledge& knowledge = partnerKnowledge[i];
        
        // Color hints
        for (Color color = RED; color <= BLUE; ++color) {
            if (!knowledge.possibleColors[static_cast<int>(color)]) {
                continue;
            }
            
            double benefit = 0.0;
            
            // New information about this card
            if (std::count(knowledge.possibleColors.begin(), knowledge.possibleColors.end(), true) > 1) {
                if (card.color == color && server.pileOf(color).nextValueIs(card.value)) {
                    benefit += 2.0;
                } else {
                    benefit += 1.0;
                }
            }
            
            // Completes knowledge of a playable card
            if (std::count(knowledge.possibleValues.begin(), knowledge.possibleValues.end(), true) == 1 && 
                card.color == color && 
                server.pileOf(color).nextValueIs(card.value)) {
                benefit += 3.0;
            }
            
            // Identifies unplayable/discardable card
            // if (card.color == color && 
            //     server.pileOf(color).contains(card.value)) {
            //     benefit += 1.0;
            // }
            
            if (benefit > 0) {
                possibleHints.push_back({
                    (me_ + 1) % numPlayers_,
                    true,
                    static_cast<int>(color),
                    benefit,
                    true,
                    std::count(knowledge.possibleValues.begin(), knowledge.possibleValues.end(), true) == 1,
                    server.pileOf(color).contains(card.value)
                });
            }
        }
        
        // Value hints
        for (int value = 1; value <= 5; value++) {
            if (!knowledge.possibleValues[value - 1]) {
                continue;
            }
            
            double benefit = 0.0;
            
            // New information about this card
            if (std::count(knowledge.possibleValues.begin(), knowledge.possibleValues.end(), true) > 1) {
                if (card.value == value && 
                server.pileOf(static_cast<Color>(
                    std::find(knowledge.possibleColors.begin(), 
                             knowledge.possibleColors.end(), 
                             true) - knowledge.possibleColors.begin()
                )).nextValueIs(value)) {
                    benefit += 2.0;
                } else {
                    benefit += 1.0;
                }
            }
            
            // Completes knowledge of a playable card
            if (std::count(knowledge.possibleColors.begin(), knowledge.possibleColors.end(), true) == 1 && 
                card.value == value && 
                server.pileOf(static_cast<Color>(
                    std::find(knowledge.possibleColors.begin(), 
                             knowledge.possibleColors.end(), 
                             true) - knowledge.possibleColors.begin()
                )).nextValueIs(value)) {
                benefit += 3.0;
            }
            
            // Identifies unplayable card (too low)
            // if (card.value == value) {
            //     bool isUnplayable = false;
            //     for (Color color = RED; color <= BLUE; ++color) {
            //         if (knowledge.possibleColors[static_cast<int>(color)] && 
            //             server.pileOf(color).contains(value)) {
            //             isUnplayable = true;
            //             break;
            //         }
            //     }
            //     if (isUnplayable) {
            //         benefit += 1.0;
            //     }
            // }
            
            // Extra value for low number cards since they're needed first
            if (value <= 2 && card.value == value) {
                benefit += 0.5;
            }
            
            if (benefit > 0) {
                possibleHints.push_back({
                    (me_ + 1) % numPlayers_,
                    false,
                    value,
                    benefit,
                    true,
                    std::count(knowledge.possibleColors.begin(), knowledge.possibleColors.end(), true) == 1,
                    false  // Only color hints can directly identify discardable cards
                });
            }
        }
    }
    
    // Give the most beneficial hint
    if (!possibleHints.empty()) {
        auto bestHint = std::max_element(possibleHints.begin(), possibleHints.end(),
            [](const HintValue& a, const HintValue& b) {
                return a.benefit < b.benefit;
            });
            
        if (bestHint->benefit > 0) {
            if (bestHint->isColor) {
                // Check if any card has this color before giving hint
                bool hasColor = false;
                for (const Card& card : partnerHand) {
                    if (card.color == static_cast<Color>(bestHint->value)) {
                        hasColor = true;
                        break;
                    }
                }
                if (!hasColor) return false;
                server.pleaseGiveColorHint(bestHint->player, static_cast<Color>(bestHint->value));
            } else {
                // Check if any card has this value before giving hint
                bool hasValue = false;
                for (const Card& card : partnerHand) {
                    if (card.value == bestHint->value) {
                        hasValue = true;
                        break;
                    }
                }
                if (!hasValue) return false;
                server.pleaseGiveValueHint(bestHint->player, static_cast<Value>(bestHint->value));
            }
            return true;
        }
    }
    
    return false;
}

// Helper function for when we must give a hint
bool MetaBot::giveFallbackHint(Server& server) {
    int nextPlayer = (me_ + 1) % numPlayers_;
    const auto& partnerHand = server.handOfPlayer(nextPlayer);
    const auto& partnerKnowledge = handKnowledge_[nextPlayer];
    if (!partnerHand.empty()) {
        // Try to give new hint starting from oldest card if possible
        for (int i = 0; i < partnerHand.size(); i++) {
            const Card& card = partnerHand[i];
            const CardKnowledge& knowledge = partnerKnowledge[i];
            if (std::count(knowledge.possibleColors.begin(), knowledge.possibleColors.end(), true) > 1) {
                server.pleaseGiveColorHint(nextPlayer, card.color);
                return true;
            }
            if (std::count(knowledge.possibleValues.begin(), knowledge.possibleValues.end(), true) > 1) {
                server.pleaseGiveValueHint(nextPlayer, card.value);
                return true;
            }
        }
        //Repeat color hint about oldest card if no other choice
        server.pleaseGiveColorHint(nextPlayer, partnerHand[0].color);
        return true;
    }
    return false;
}

bool MetaBot::tryDiscardSafeCard(Server& server) {
    // Can't discard if we have all hint stones
    if (!server.discardingIsAllowed()) {
        return false;
    }

    // Look for safe discards starting from oldest card
    for (int i = 0; i < server.sizeOfHandOfPlayer(me_); i++) {
        const CardKnowledge& knowledge = handKnowledge_[me_][i];
        
        // A card is safe to discard if:
        // 1. We know it's discardable (already played or duplicate)
        // 2. It's definitely not critical
        if (knowledge.isDiscardable) {
            server.pleaseDiscard(i);
            return true;
        }
    }
    
    return false;
}

// Helper function for risky plays
int MetaBot::findBestRiskyPlay(Server& server) {
    int bestIndex = -1;
    double bestProb = 0.5; // Minimum threshold for risky plays
    
    for (int i = 0; i < server.sizeOfHandOfPlayer(me_); i++) {
        const CardKnowledge& knowledge = handKnowledge_[me_][i];
        double playProb = calculatePlayProbability(knowledge);
        
        if (playProb > bestProb) {
            bestProb = playProb;
            bestIndex = i;
        }
    }
    
    return bestIndex;
}

bool MetaBot::isCardPlayable(const Hanabi::Card& card) const {
    return server_->pileOf(card.color).nextValueIs(card.value);
}

bool MetaBot::isCardCritical(const Hanabi::Card& card) const {
    // A card is critical if:
    // 1. It hasn't been played yet
    // 2. All other copies have been discarded
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

double MetaBot::calculatePlayProbability(const CardKnowledge& knowledge) const {
    // Fast path: if we already know the card is playable
    if (knowledge.isPlayable) {
        return 1.0;
    }
    
    // Track cards we can see in other players' hands and discards
    std::map<Card, int> visibleCards;
    
    // Add discarded cards to our visible count
    for (const Card& card : server_->discards()) {
        visibleCards[card]++;
    }
    
    // Add cards in other players' hands
    for (int p = 0; p < numPlayers_; p++) {
        if (p == me_) continue;  // Skip our own hand
        for (const Card& card : server_->handOfPlayer(p)) {
            visibleCards[card]++;
        }
    }

    int playableCombs = 0;
    int totalCombs = 0;
    
    // Check each possible color/value combination
    for (int c = 0; c < knowledge.possibleColors.size(); c++) {
        if (!knowledge.possibleColors[c]) continue;
        Color color = static_cast<Color>(c);
        
        for (int v = 0; v < knowledge.possibleValues.size(); v++) {
            if (!knowledge.possibleValues[v]) continue;
            Value value = static_cast<Value>(v + 1);
            
            // Create the hypothetical card
            Card potentialCard(color, value);
            
            // Check if all copies of this card are already visible
            int visibleCount = visibleCards[potentialCard];
            if (visibleCount >= potentialCard.count()) {
                // This combination is impossible - skip it
                continue;
            }

            // Check prerequisites for this card
            bool prerequisitesAvailable = true;
            const Pile& pile = server_->pileOf(color);
            for (int prereqValue = pile.size() + 1; prereqValue < value; prereqValue++) {
                Card prereq(color, prereqValue);
                int visiblePrereqCount = visibleCards[prereq];
                
                // If all copies of a prerequisite are discarded, this card can never be played
                if (visiblePrereqCount == prereq.count() && 
                    !pile.contains(prereqValue)) {
                    prerequisitesAvailable = false;
                    break;
                }
            }
            
            if (!prerequisitesAvailable) {
                continue;  // Skip this combination if prerequisites are gone
            }

            totalCombs++;
            if (server_->pileOf(color).nextValueIs(value)) {
                playableCombs++;
            }
        }
    }
    
    double playProbability = totalCombs > 0 ? static_cast<double>(playableCombs) / totalCombs : 0.0;
    if (knowledge.hinted.color > 1  || knowledge.hinted.value > 0) {
        playProbability += 0.15;
    }
    return std::min(1.0, playProbability);
}

// Core strategy implementation

void MetaBot::pleaseObserveBeforeMove(const Hanabi::Server &server) {
    // Store server reference and assert correct player
    server_ = &server;
    assert(server.whoAmI() == me_);

    // Update hand size
    for (int p = 0; p < numPlayers_; p++) {
        handKnowledge_[p].resize(server.sizeOfHandOfPlayer(p));
    }

    // Update knowledge about playability and discardability
    for (int p = 0; p < numPlayers_; p++) {
        for (int c = 0; c < handKnowledge_[p].size(); c++) {
            CardKnowledge& knowledge = handKnowledge_[p][c];

            // If it's not our card, we can see it
            if (p != me_) {
                Card card = server.handOfPlayer(p)[c];
                knowledge.isPlayable = isCardPlayable(card);
                knowledge.isDiscardable = !isCardCritical(card) || 
                    server.pileOf(card.color).contains(card.value);
            }
        }
    }
}

void MetaBot::pleaseMakeMove(Server& server) {
    // Update current phase based on game state
    currentPhase_ = determineGamePhase(server);
    
    switch (currentPhase_) {
        case GamePhase::OPENING: {
            // Opening prioritizes setting up plays and information gathering
            if (tryPlaySafeCard(server)) return;
            
            // Early game - use hints liberally to establish knowledge
            if (server.hintStonesRemaining() >= 3) {
                if (tryGiveInformativeHint(server)) return;
            }
            
            // Only discard if we have to
            if (server.discardingIsAllowed()) {
                if (tryDiscardSafeCard(server)) return;
                // Prefer discarding oldest cards if no safe discard is possible
                server.pleaseDiscard(0);
            } else {
                // Must give a hint - prefer one that gives most information
                if (tryGiveInformativeHint(server)) return;
                giveFallbackHint(server);
            }
            break;
        }

        case GamePhase::MIDGAME: {
            // Midgame balances playing riskier cards with maintaining hint stones
            if (tryPlaySafeCard(server)) return;

            // Try to give valuable hints if we have stones to spare
            if (server.hintStonesRemaining() >= 2) {
                if (tryGiveInformativeHint(server)) return;
            }

            // More willing to discard in midgame
            if (server.discardingIsAllowed()) {
                if (tryDiscardSafeCard(server)) return;
                // Prefer discarding oldest cards if no safe discard is possible
                server.pleaseDiscard(0);
            } else {
                if (tryGiveInformativeHint(server)) return;
                giveFallbackHint(server);
            }
            break;
        }

        case GamePhase::ENDGAME: {
            // Try risky play -0.5 threshold needed to reduce randomness
            int bestRiskyPlayIndex = findBestRiskyPlay(server);
            if (bestRiskyPlayIndex != -1) {
                server.pleasePlay(bestRiskyPlayIndex);
                return;
            }

            // Prioritize hints that enable immediate plays
            if (tryGiveInformativeHint(server)) return;

            // Last resort - give fallback hint or play oldest card
            if (server.hintStonesRemaining() > 0) {
                giveFallbackHint(server);
            } else {
                server.pleasePlay(0);
            }
            break;
        }

        case GamePhase::CRISIS: {
            // Crisis mode - focus on not losing
            if (tryPlaySafeCard(server)) return;  // Only play if certain
            
            // Save hint stones where possible
            if (server.discardingIsAllowed() && server.hintStonesRemaining() <= 4) {
                if (tryDiscardSafeCard(server)) return;
            }

            // Only give hints that enable immediate plays or save critical cards
            if (server.hintStonesRemaining() > 0) {
                if (tryGiveInformativeHint(server)) return;
            }

            // Last resort - prefer discarding to risky plays
            if (server.discardingIsAllowed()) {
                server.pleaseDiscard(0);
            } else {
                giveFallbackHint(server);
            }
            break;
        }
    }
}

void MetaBot::pleaseObserveBeforeDiscard(const Hanabi::Server &server, int from, int card_index) {
    // Shift knowledge for remaining cards
    auto& playerKnowledge = handKnowledge_[from];
    if (card_index < playerKnowledge.size() - 1) {
        for (int i = card_index; i < playerKnowledge.size() - 1; i++) {
            playerKnowledge[i] = playerKnowledge[i + 1];
        }
    }
    playerKnowledge.pop_back();

    // If there are cards left in deck, add new blank knowledge
    if (server.cardsRemainingInDeck() > 0) {
        playerKnowledge.push_back(CardKnowledge());
    }
}

void MetaBot::pleaseObserveBeforePlay(const Hanabi::Server &server, int from, int card_index) {
    // Handle knowledge update same as discard
    pleaseObserveBeforeDiscard(server, from, card_index);
}

void MetaBot::pleaseObserveColorHint(const Hanabi::Server &server, int from, int to, 
                                    Hanabi::Color color, Hanabi::CardIndices card_indices) {
    // Update knowledge for each card in player's hand
    for (int i = 0; i < server.sizeOfHandOfPlayer(to); i++) {
        handKnowledge_[to][i].updateFromHint(true, static_cast<int>(color), 
                                           card_indices.contains(i));
    }

    // After hint, update playability and discardability for the affected cards
    for (int i = 0; i < handKnowledge_[to].size(); i++) {
        if (card_indices.contains(i)) {
            CardKnowledge& knowledge = handKnowledge_[to][i];
            //Increment hint counter 
            knowledge.hinted.color++;
            
            // If only one color and value possible, we can check playability
            int possibleColors = std::count(knowledge.possibleColors.begin(), 
                                          knowledge.possibleColors.end(), true);
            int possibleValues = std::count(knowledge.possibleValues.begin(), 
                                          knowledge.possibleValues.end(), true);
            
            if (possibleColors == 1 && possibleValues == 1) {
                // Find the card's color and value
                int cardColor = std::find(knowledge.possibleColors.begin(), 
                                        knowledge.possibleColors.end(), true) - 
                              knowledge.possibleColors.begin();
                int cardValue = std::find(knowledge.possibleValues.begin(), 
                                        knowledge.possibleValues.end(), true) - 
                              knowledge.possibleValues.begin() + 1;
                
                Card card(static_cast<Color>(cardColor), cardValue);
                knowledge.isPlayable = isCardPlayable(card);
                knowledge.isDiscardable = !isCardCritical(card) || 
                    server.pileOf(card.color).contains(card.value);
            }
        }
    }
}

void MetaBot::pleaseObserveValueHint(const Hanabi::Server &server, int from, int to,
                                    Hanabi::Value value, Hanabi::CardIndices card_indices) {
    // Update knowledge for each card in player's hand
    for (int i = 0; i < server.sizeOfHandOfPlayer(to); i++) {
        handKnowledge_[to][i].updateFromHint(false, static_cast<int>(value), 
                                           card_indices.contains(i));
    }

    // After hint, update playability and discardability same as color hint
    for (int i = 0; i < handKnowledge_[to].size(); i++) {
        if (card_indices.contains(i)) {
            CardKnowledge& knowledge = handKnowledge_[to][i];
            //Increment hint counter 
            knowledge.hinted.value++;
            
            int possibleColors = std::count(knowledge.possibleColors.begin(), 
                                          knowledge.possibleColors.end(), true);
            int possibleValues = std::count(knowledge.possibleValues.begin(), 
                                          knowledge.possibleValues.end(), true);
            
            if (possibleColors == 1 && possibleValues == 1) {
                int cardColor = std::find(knowledge.possibleColors.begin(), 
                                        knowledge.possibleColors.end(), true) - 
                              knowledge.possibleColors.begin();
                int cardValue = std::find(knowledge.possibleValues.begin(), 
                                        knowledge.possibleValues.end(), true) - 
                              knowledge.possibleValues.begin() + 1;
                
                Card card(static_cast<Color>(cardColor), cardValue);
                knowledge.isPlayable = isCardPlayable(card);
                knowledge.isDiscardable = !isCardCritical(card) || 
                    server.pileOf(card.color).contains(card.value);
            }
        }
    }
}

void MetaBot::pleaseObserveAfterMove(const Hanabi::Server &server) {
    // Nothing special needed after move
    assert(server.whoAmI() == me_);
}

MetaBot* MetaBot::clone() const {
    // Create new bot with same initial parameters
    MetaBot* b = new MetaBot(me_, numPlayers_, handKnowledge_[0].size());
    
    // Copy all member variables
    b->server_ = this->server_;
    b->currentPhase_ = this->currentPhase_;

    // Deep copy of hand knowledge
    assert(this->handKnowledge_.size() == b->handKnowledge_.size());
    for (int i = 0; i < handKnowledge_.size(); i++) {
        b->handKnowledge_[i].clear();
        for (const auto& knol : handKnowledge_[i]) {
            b->handKnowledge_[i].push_back(knol);
        }
    }

    // Copy permissive flag from base Bot class
    b->permissive_ = this->permissive_;
    
    return b;
}