#pragma once
#include "Hanabi.h"
#include <map>
#include <vector>
#include <memory>

namespace PileB {
    // Pile status tracking
    struct PileStatus {
        Hanabi::Color color;
        int height;
        bool isActive;  // Whether we're currently focusing on this pile
        int nextValueNeeded;
    };

    struct HintOption {
    int targetPlayer;
    bool useColor;
    int value;
    double baseValue;
    std::vector<int> affectedCards;
    // Track new information provided by this hint
    std::vector<int> newlyInformedCards;  // Cards that get new information
};

    // Card knowledge tracking
    struct CardKnowledge {
        bool isPlayable;
        bool isDiscardable;
        std::vector<bool> possibleColors;
        std::vector<bool> possibleValues;
        int lastHintTurn;
        CardKnowledge();
        void updateFromHint(bool isColor, int value, bool positive);
    };
}

class PileBot final : public Hanabi::Bot {
public:
    PileBot(int index, int numPlayers, int handSize);
    void pleaseObserveBeforeMove(const Hanabi::Server &) override;
    void pleaseMakeMove(Hanabi::Server &) override;
    void pleaseObserveBeforeDiscard(const Hanabi::Server &, int from, int card_index) override;
    void pleaseObserveBeforePlay(const Hanabi::Server &, int from, int card_index) override;
    void pleaseObserveColorHint(const Hanabi::Server &, int from, int to, Hanabi::Color color, Hanabi::CardIndices card_indices) override;
    void pleaseObserveValueHint(const Hanabi::Server &, int from, int to, Hanabi::Value value, Hanabi::CardIndices card_indices) override;
    void pleaseObserveAfterMove(const Hanabi::Server &) override;
    PileBot* clone() const override;

private:
    // Core member variables
    int me_;
    int numPlayers_;
    int currentTurn_;
    const Hanabi::Server* server_;
    std::vector<std::vector<PileB::CardKnowledge>> handKnowledge_;

    std::vector<PileB::PileStatus> getPrioritizedPiles(const Hanabi::Server& server) const;
    Hanabi::Color getMostAdvancedPlayablePile(const Hanabi::Server& server) const;

    // Strategy implementation methods
    bool tryPlayPriorityCard(Hanabi::Server& server);
    bool tryGivePriorityHint(Hanabi::Server& server);
    bool trySafePriorityDiscard(Hanabi::Server& server);

    // Utility methods
    double calculatePlayProbability(const PileB::CardKnowledge& card, Hanabi::Color targetColor) const;
    bool isCardPlayable(const Hanabi::Card& card) const;
    bool isCardCritical(const Hanabi::Card& card) const;
    bool willCompletePile(const Hanabi::Card& card) const;
    bool isPartOfPrioritizedPile(const Hanabi::Card& card) const;
    bool shouldPreserveCard(const Hanabi::Card& card) const;
    double evaluateHintValue(int to, Hanabi::Color color) const;
    double evaluateHintValue(int to, Hanabi::Value value) const;
};