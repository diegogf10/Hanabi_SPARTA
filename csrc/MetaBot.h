#pragma once
#include <map>
#include <vector>
#include <memory>
#include "Hanabi.h"

namespace MetaB {
    // Game phases that determine strategic approach
    enum class GamePhase {
        OPENING,    // Early game: focus on information gathering
        MIDGAME,    // Middle game: efficient execution
        ENDGAME,    // Late game: careful planning
        CRISIS      // Emergency situation: survival tactics
    };

    // Structure to track game state metrics
    struct GameMetrics {
        double deckDepletion;      // How much of deck is used (0-1)
        double fireworkProgress;    // Overall progress (0-1)
        double hintAvailability;   // Hint stones available (0-1)
        double lifeBuffer;         // Lives remaining (0-1)
        double knownCards;         // Proportion of known cards (0-1)
    };

    // Knowledge tracking
    struct CardKnowledge {
        bool isPlayable;
        bool isDiscardable;
        std::vector<bool> possibleColors;
        std::vector<bool> possibleValues;
        struct {
            int value = 0;    // Number of times value has been hinted
            int color = 0;    // Number of times color has been hinted
        } hinted;
        CardKnowledge();
        void updateFromHint(bool isColor, int value, bool positive);
    };

    struct HintValue {
        int player;
        bool isColor;
        int value;
        double benefit;
        bool providesNewInformation;  
        bool completesExistingKnowledge; 
        bool enablesDiscard; 
    };


} //namespace MetaB

class MetaBot final : public Hanabi::Bot {
    // Core member variables
    int me_;
    int numPlayers_;
    MetaB::GamePhase currentPhase_;
    const Hanabi::Server* server_;
    std::vector<std::vector<MetaB::CardKnowledge>> handKnowledge_;

    // Phase determination methods
    MetaB::GamePhase determineGamePhase(const Hanabi::Server& server);
    MetaB::GameMetrics calculateGameMetrics(const Hanabi::Server& server);
    bool isCrisisPhase(const MetaB::GameMetrics& metrics) const;

    // Strategy implementation methods
    bool tryPlaySafeCard(Hanabi::Server& server);
    bool tryGiveInformativeHint(Hanabi::Server& server);
    bool giveFallbackHint(Hanabi::Server& server);
    bool tryDiscardSafeCard(Hanabi::Server& server);
    int findBestRiskyPlay(Hanabi::Server& server);
    

    // Utility methods
    double calculatePlayProbability(const MetaB::CardKnowledge& card) const;
    bool isCardPlayable(const Hanabi::Card& card) const;
    bool isCardCritical(const Hanabi::Card& card) const; 

public:
    MetaBot(int index, int numPlayers, int handSize);
    void pleaseObserveBeforeMove(const Hanabi::Server &) override;
    void pleaseMakeMove(Hanabi::Server &) override;
    void pleaseObserveBeforeDiscard(const Hanabi::Server &, int from, int card_index) override;
    void pleaseObserveBeforePlay(const Hanabi::Server &, int from, int card_index) override;
    void pleaseObserveColorHint(const Hanabi::Server &, int from, int to, Hanabi::Color color, Hanabi::CardIndices card_indices) override;
    void pleaseObserveValueHint(const Hanabi::Server &, int from, int to, Hanabi::Value value, Hanabi::CardIndices card_indices) override;
    void pleaseObserveAfterMove(const Hanabi::Server &) override;
    MetaBot* clone() const override;

};