#pragma once
#include "Hanabi.h"
#include <map>
#include <vector>
#include <memory>

namespace SignB {
    enum class SignalType {
        DIRECT,      // Single hint about playable card
        INDIRECT,    // Information through omission
        COMPOSITE,   // Multiple hints forming one message
        CONTEXTUAL   // Timing/situation based signals
    };

    // Card knowledge tracking
    struct CardKnowledge {
        bool isPlayable;
        bool isDiscardable;
        std::vector<bool> possibleColors;
        std::vector<bool> possibleValues;
        struct {
            int value = 0;     // Number of value hints received
            int color = 0;     // Number of color hints received
            bool composite = false;  // Part of composite signal
        } hinted;

        CardKnowledge();
        void updateFromHint(bool isColor, int value, bool positive);
    };

    // Signal interpretation result
    struct SignalInterpretation {
        SignalType type;
        double confidence;
        int cardIndex;
        bool isPlayable;
        bool isValuable;
    };

    // Track received hints
    struct ReceivedHint {
        int fromPlayer;
        bool isColor;
        int value;
        std::vector<int> cardIndices;
        int turnReceived;
    };
}

class SignalBot final : public Hanabi::Bot {
private:
    // Core member variables
    int me_;
    int numPlayers_;
    const Hanabi::Server* server_;
    std::vector<std::vector<SignB::CardKnowledge>> handKnowledge_;
    std::vector<SignB::ReceivedHint> recentHints_;
    int currentTurn_ = 0;

    // Signal interpretation methods
    SignB::SignalInterpretation interpretDirectSignal(
        const Hanabi::Server& server, int from, int to, const Hanabi::CardIndices& card_indices);
    SignB::SignalInterpretation interpretIndirectSignal(
        const Hanabi::Server& server, int from, int to, const Hanabi::CardIndices& card_indices);
    SignB::SignalInterpretation interpretCompositeSignal(
        const Hanabi::Server& server, int from, int to, const Hanabi::CardIndices& card_indices);
    SignB::SignalInterpretation interpretContextualSignal(
        const Hanabi::Server& server, int from, int to, const Hanabi::CardIndices& card_indices);

    // Strategy methods
    bool handleSignalPlay(Hanabi::Server& server);  // Try to play based on received signals
    bool handleSignalGive(Hanabi::Server& server);  // Try to give signal about important cards
    bool tryPlaySafeCard(Hanabi::Server& server);   // Play cards we're confident about
    bool tryDiscardSafeCard(Hanabi::Server& server); // Discard safely if possible

    // Utility methods
    double calculatePlayProbability(const SignB::CardKnowledge& card) const;
    bool isCardPlayable(const Hanabi::Card& card) const;
    bool isCardCritical(const Hanabi::Card& card) const;
    void updateCardPlayability();  // Update knowledge after each move
    void cleanupOldHints();       // Remove old hints from history

public:
    SignalBot(int index, int numPlayers, int handSize);
    void pleaseObserveBeforeMove(const Hanabi::Server &) override;
    void pleaseMakeMove(Hanabi::Server &) override;
    void pleaseObserveBeforeDiscard(const Hanabi::Server &, int from, int card_index) override;
    void pleaseObserveBeforePlay(const Hanabi::Server &, int from, int card_index) override;
    void pleaseObserveColorHint(const Hanabi::Server &, int from, int to, Hanabi::Color color, Hanabi::CardIndices card_indices) override;
    void pleaseObserveValueHint(const Hanabi::Server &, int from, int to, Hanabi::Value value, Hanabi::CardIndices card_indices) override;
    void pleaseObserveAfterMove(const Hanabi::Server &) override;
    SignalBot* clone() const override;
};