#pragma once
#include "Hanabi.h"
#include "BotUtils.h"
#include <map>
#include <vector>
#include <memory>
#include <deque>

namespace NetB {
    // Classify different playing styles
    enum class PlayStyleType {
        AGGRESSIVE,    // Takes more risks, plays cards with less information
        CONSERVATIVE, // Waits for complete information before playing
        HINT_FOCUSED, // Prioritizes giving hints over discarding
        DISCARD_FOCUSED // Prefers discarding to maintain hint flow
    };

    // Track hint patterns 
    struct HintPattern {
        bool isColor;
        int value;
        int targetCardIndex;
        bool resultedInPlay;  // Did this hint lead to a card being played?
        double timeToAction;  // How many moves until the hint was acted upon
    };

    // Maintain history of observed moves and their outcomes
    struct MoveHistory {
        std::deque<Move> recentMoves;
        static constexpr int MAX_HISTORY = 10;
        std::map<MoveType, int> moveFrequency;
        std::vector<HintPattern> hintPatterns;
        
        void addMove(const Move& move) {
            if (recentMoves.size() >= MAX_HISTORY) {
                recentMoves.pop_front();
            }
            recentMoves.push_back(move);
            moveFrequency[move.type]++;
        }
    };

    // Track play style metrics
    struct PlayStyle {
        double riskTolerance;     // 0-1, higher means more aggressive plays
        double hintEfficiency;    // 0-1, how well hints are used
        double discardFrequency;  // 0-1, preference for discarding
        int consecutiveHints;
        int consecutiveDiscards;
        PlayStyleType dominantStyle;
        
        // Initialize with neutral values
        PlayStyle() : riskTolerance(0.5), hintEfficiency(0.5), 
                     discardFrequency(0.5), consecutiveHints(0),
                     consecutiveDiscards(0), dominantStyle(PlayStyleType::CONSERVATIVE) {}
    };

    // Basic card knowledge tracking
    struct CardKnowledge {
        bool isPlayable;
        bool isDiscardable;
        std::vector<bool> possibleColors;
        std::vector<bool> possibleValues;
        int numHints;  // Number of hints received about this card
        double playProbability;
        double criticalProbability;
        
        CardKnowledge();
        void updateFromHint(bool isColor, int value, bool positive);
        void updatePlayability(const Hanabi::Server& server);


    };
}

class NetworkBot final : public Hanabi::Bot {
private:
    // Core member variables
    int me_;
    int partner_;  // In 2-player game, this is always 1-me_
    const Hanabi::Server* server_;
    
    // Track knowledge and patterns
    std::vector<std::vector<NetB::CardKnowledge>> handKnowledge_;
    std::vector<NetB::PlayStyle> playerStyles_;
    std::vector<NetB::MoveHistory> moveHistory_;
    
    // Strategy thresholds (adjusted based on partner's style)
    double playThreshold_;    // Minimum probability to play a card
    double hintThreshold_;    // Minimum value to give a hint
    double discardThreshold_; // Minimum safety to discard
    
    // Pattern analysis methods
    void updatePartnerStyle(const Hanabi::Server& server, const Move& move);
    void adaptThresholds();
    double calculateRiskLevel(const Move& move) const;
    double calculateHintEfficiency(const Move& move) const;
    NetB::PlayStyleType determinePlayStyle(const NetB::PlayStyle& style) const;
    
    // Move analysis
    struct MoveAnalysis {
        double risk;
        double informationGain;
        double strategicValue;
        double successProbability;
    };
    
    MoveAnalysis analyzePotentialMove(const Hanabi::Server& server, const Move& move) const;
    double calculateMoveScore(const MoveAnalysis& analysis, const NetB::PlayStyle& style) const;
    
    // Strategy methods
    bool tryPlayCard(Hanabi::Server& server);
    bool tryGiveHint(Hanabi::Server& server);
    bool tryDiscard(Hanabi::Server& server);
    bool tryGiveOneStoneHint(Hanabi::Server& server);
    
    // Utility methods
    std::vector<Move> generatePossibleMoves(const Hanabi::Server& server) const;
    double calculatePlayProbability(const NetB::CardKnowledge& knowledge) const;
    bool isCardCritical(const Hanabi::Card& card) const;
    double evaluateHintValue(const Move& hint) const;
    void updateCardKnowledge();
    bool isOneHintStoneLeft() const {
        return server_->hintStonesRemaining() == 1;
    }

    // Historical analysis
    void analyzeHintPatterns();
    void updateHintEfficiency(const Move& move, bool resultedInPlay);
    double predictMoveSuccess(const Move& move) const;

public:
    NetworkBot(int index, int numPlayers, int handSize);
    void pleaseObserveBeforeMove(const Hanabi::Server &) override;
    void pleaseMakeMove(Hanabi::Server &) override;
    void pleaseObserveBeforeDiscard(const Hanabi::Server &, int from, int card_index) override;
    void pleaseObserveBeforePlay(const Hanabi::Server &, int from, int card_index) override;
    void pleaseObserveColorHint(const Hanabi::Server &, int from, int to, Hanabi::Color color, Hanabi::CardIndices card_indices) override;
    void pleaseObserveValueHint(const Hanabi::Server &, int from, int to, Hanabi::Value value, Hanabi::CardIndices card_indices) override;
    void pleaseObserveAfterMove(const Hanabi::Server &) override;
    NetworkBot* clone() const override;
};