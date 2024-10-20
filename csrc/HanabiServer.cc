// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#include <algorithm>
#include <cassert>
#include <ostream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>
#include "Hanabi.h"

#ifdef HANABI_SERVER_NDEBUG
#define HANABI_SERVER_ASSERT(x, msg) (void)0
#else
#define HANABI_SERVER_ASSERT(x, msg) do { if (!(x)) throw ServerError(msg); } while (0)
#endif

namespace Params {

std::string getParameterString(const std::string &name, std::string default_val, const std::string help) {
  static std::map<std::string, std::string> memoized;
  if (memoized.count(name)) {
    return memoized.at(name);
  }
  char *val = getenv(name.c_str());
  std::string ret = (val && std::string(val) != "") ? std::string(val) : default_val;
  std::cerr << name << ": " << ret << std::endl;
  if (help != "") {
    std::cerr << "\t" << help << std::endl;
  }
  memoized[name] = ret;
  return ret;
}

int getParameterInt(const std::string &name, int default_val, const std::string help) {
  std::string val = getParameterString(name, std::to_string(default_val), help);
  return stoi(val);
}

float getParameterFloat(const std::string &name, float default_val, const std::string help) {
  std::string val = getParameterString(name, std::to_string(default_val), help);
  return std::stof(val);
}

}


using namespace HanabiParams;

static std::string nth(int n, int total)
{
    if (total == 5) {
        switch (n) {
            case 0: return "O";
            case 1: return "SO";
            case 2: return "M";
            case 3: return "SN";
            default: assert(n == 4); return "N";
        }
    } else if (total == 4) {
        switch (n) {
            case 0: return "O";
            case 1: return "SO";
            case 2: return "SN";
            default: assert(n == 3); return "N";
        }
    } else {
        switch (n) {
            case 0: return "O";
            case 1: return "N";
            default: assert(n == 2); return "N";
        }
    }
}

static std::string nth(const Hanabi::CardIndices& ns, int total)
{
    std::string result;
    assert(!ns.empty());
    switch (ns.size()) {
        case 1: return nth(ns[0], total);
        case 2: return nth(ns[0], total) + ", " + nth(ns[1], total);
        case 3: return nth(ns[0], total) + ", " + nth(ns[1], total) + ", " + nth(ns[2], total);
        default:
            assert(ns.size() == 4);
            return nth(ns[0], total) + ", " + nth(ns[1], total) + ", " + nth(ns[2], total) + ", " + nth(ns[3], total);
    }
}

static std::string colorname(Hanabi::Color color)
{
    switch (color) {
        case Hanabi::RED: return "r";
        case Hanabi::WHITE: return "w";
        case Hanabi::YELLOW: return "y";
        case Hanabi::GREEN: return "g";
        case Hanabi::BLUE: return "b";
        case Hanabi::INVALID_COLOR: return "Invalid_color";
    }
    assert(false);
    return "ERROR"; // silence compiler
}

namespace Hanabi {

Card::Card(Color c, Value v): color(c), value(v) { assert(1 <= value && value <= 5); }
Card::Card(Color c, int v): color(c), value(Value(v)) { assert(1 <= value && value <= 5); }

int Card::count() const
{
    switch (value) {
        case 1: return 3;
        case 2: case 3: case 4: return 2;
        case 5: return 1;
        default: HANABI_SERVER_ASSERT(false, "invalid card value");
    }
    return -1; // silence compiler
}

std::string Card::toString() const
{
    std::string result;
    result += (char)('0' + this->value);
    result += (char)(colorname(this->color)[0]);
    return result;
}

Card Pile::topCard() const
{
    HANABI_SERVER_ASSERT(size_ != 0, "empty pile has no top card");
    assert(1 <= size_ && size_ <= 5);
    return Card(color, size_);
}

void Pile::increment_()
{
    assert(0 <= size_ && size_ <= 4);
    ++size_;
}

/* virtual destructor */
Bot::~Bot() { }

/* Hanabi::Card has no default constructor */
Server::Server(): log_(nullptr), activeCard_(RED,1) { }

bool Server::gameOver() const
{
    /* The game ends if there are no more cards to draw... */
    if (this->deck_.empty() && finalCountdown_ == numPlayers_+1) return true;
    /* ...no more mulligans available... */
    if (this->mulligansRemaining_ == 0) return true;
    /* ...or if the piles are all complete. */
    if (this->currentScore() == 5*NUMCOLORS) return true;
    /* Otherwise, the game has not ended. */
    return false;
}

int Server::currentScore() const
{
    if(mulligansRemaining_ == 0 && BOMB0) {
      return 0;
    }

    int sum = 0;
    for (int color = 0; color < NUMCOLORS; ++color) {
        const Pile &pile = this->piles_[color];
        if (!pile.empty()) {
            sum += pile.topCard().value;
        }
    }
    // add a little penalty to discouurage mulligans based on equivalent choices
    if (mulligansRemaining_ == 0) {
      sum = std::max(sum - BOMBD, 0);
    }
    return sum;
}

void Server::setLog(std::ostream *logStream)
{
    this->log_ = logStream;
}

void Server::srand(unsigned int seed)
{
    this->seed_ = seed;
    this->rand_.seed(seed);
}

void Server::sqa(unsigned int qa) 
{
    this->qa_ = qa;
}

template<class It, class Gen>
static void portable_shuffle(It first, It last, Gen& g)
{
    const int n = (last - first);
    for (int i=0; i < n; ++i) {
        int j = (g() % (i + 1));
        if (j != i) {
            using std::swap;
            swap(first[i], first[j]);
        }
    }
}

int Server::runGame(const BotFactory &botFactory, int numPlayers)
{
    return this->runGame(botFactory, numPlayers, std::vector<Card>());
}

int Server::runGame(const BotFactory &botFactory, int numPlayers, const std::vector<Card>& stackedDeck)
{
  numPlayers_ = numPlayers;
  std::vector<Bot*> players(numPlayers);
  for (int i=0; i < numPlayers; ++i) {
      players[i] = botFactory.create(i, numPlayers, handSize());
  }
  int score = runGame(players, stackedDeck);
  for (int i=0; i < numPlayers; ++i) {
      botFactory.destroy(players[i]);
  }
  return score;
}

int Server::runGame(std::vector<Bot*> players, const std::vector<Card>& stackedDeck)
{
    std::cerr << "Start game" << std::endl;
    /* Create and initialize the bots. */
    players_ = players;
    numPlayers_ = players.size();
    const int initialHandSize = this->handSize();

    /* Initialize the piles and stones. */
    for (Color color = RED; color <= BLUE; ++color) {
        piles_[(int)color].color = color;
        piles_[(int)color].size_ = 0;
    }
    mulligansRemaining_ = NUMMULLIGANS;
    hintStonesRemaining_ = NUMHINTS;
    finalCountdown_ = 0;

    /* Shuffle the deck. */
    if (!stackedDeck.empty()) {
        deck_ = stackedDeck;
        std::reverse(deck_.begin(), deck_.end());  /* because we pull cards from the "top" (back) of the vector */
    } else {
        deck_.clear();
        for (Color color = RED; color <= BLUE; ++color) {
            for (int value = 1; value <= 5; ++value) {
                const Card card(color, value);
                const int n = card.count();
                for (int k=0; k < n; ++k) deck_.push_back(card);
            }
        }
        portable_shuffle(deck_.begin(), deck_.end(), rand_);
    }
#ifdef CARD_ID
    int id_count = 0;
    for (auto &card: deck_) {
      card.id = id_count++;
    }
#endif
    discards_.clear();

    /* Secretly draw the starting hands. */
    hands_.resize(numPlayers_);
    for (int i=0; i < numPlayers_; ++i) {
        hands_[i].clear();
        for (int k=0; k < initialHandSize; ++k) {
            hands_[i].push_back(this->draw_());
        }
    }

    /* Run the game. */
    activeCardIsObservable_ = false;
    activePlayer_ = 0;
    movesFromActivePlayer_ = -1;
    int score = this->runToCompletion();

    return score;
}

int Server::runToCompletion() {

  std::string prevHands = "";
  int questionRound = selectQuestionRound(); // Select the round to generate the question
  if (log_) {
    *log_ << this->cardsRemainingInDeck() << " cards remaining" << std::endl;
  }

  while (!this->gameOver()) {
    if (activePlayer_ == 0 && prevHands != this->handsAsStringWithoutPlayer0()) {
        this->logHands_();
        prevHands = this->handsAsStringWithoutPlayer0();
    }
    for (int i=0; i < numPlayers_; ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveBeforeMove(*this);
    }
    observingPlayer_ = activePlayer_;
    movesFromActivePlayer_ = 0;

    //Update value of hints for consistency
    this->pleaseUpdateValuableHints();

    //In-game Q&A logic for the agent's own cards (answers based on common knowledge that can be inferred from the game state -i.e. not every bot should know the correct answer)
    if (this->qa_ == 1) {
        if (this->cardsRemainingInDeck() <= questionRound && activePlayer_ == 0) {
            Question question = this->generateRandomQuestion();
            Answer answer = processQuestion(question);
            if (question.getType() == Question::Type::COLOR) {
                (*log_) << "Is your " 
                        << nth(question.getCardPosition(), sizeOfHandOfPlayer(activePlayer_)) << " card "
                        << colorname(question.getColor()) << "?\n";
            } else {
                (*log_) << "Is your " 
                        << nth(question.getCardPosition(), sizeOfHandOfPlayer(activePlayer_)) << " card a "
                        << question.getNumber() << "?\n";
            }
            (*log_) << "answer: " << answer.answerAsString() << "\n";
            //Log different variables for dataset analysis later on
            (*log_) << "cards_remaining: " << questionRound << "\n";
            (*log_) << "question_position: " << nth(question.getCardPosition(), sizeOfHandOfPlayer(activePlayer_)) << "\n";
            if (question.getType() == Question::Type::COLOR) {
                (*log_) << "question_value: " << colorname(question.getColor()) << "\n";
            } else {
                (*log_) << "question_value: " << question.getNumber() << "\n";
            }
            // End the game after generating the question and logging the answer
            break;
        } 
    //In-game Q&A logic for the partner's cards
    } else if (this->qa_ == 2) {
        if (this->cardsRemainingInDeck() <= questionRound && activePlayer_ == 0) {
            Question question = this->generateRandomQuestion();
            //Look at relevant card in P1's hand
            Card answer_card = cheatGetHand(1)[question.getCardPosition()];
            if (question.getType() == Question::Type::COLOR) {
                (*log_) << "Is the " 
                        << nth(question.getCardPosition(), sizeOfHandOfPlayer(activePlayer_)) << " card of P1 "
                        << colorname(question.getColor()) << "?\n";
                (*log_) << "answer: " << ((answer_card.color == question.getColor()) ? "Yes" : "No") << "\n";
            } else {
                (*log_) << "Is the " 
                        << nth(question.getCardPosition(), sizeOfHandOfPlayer(activePlayer_)) << " card of P1 a "
                        << question.getNumber() << "?\n";
                (*log_) << "answer: " << ((int(answer_card.value) == question.getNumber()) ? "Yes" : "No") << "\n";

            }
            //Log different variables for dataset analysis later on
            (*log_) << "cards_remaining: " << questionRound << "\n";
            (*log_) << "question_position: " << nth(question.getCardPosition(), sizeOfHandOfPlayer(activePlayer_)) << "\n";
            if (question.getType() == Question::Type::COLOR) {
                (*log_) << "question_value: " << colorname(question.getColor()) << "\n";
            } else {
                (*log_) << "question_value: " << question.getNumber() << "\n";
            }
            // End the game after generating the question and logging the answer
            break;
        }
    //In-game Q&A logic for piles
    } else if (this->qa_ == 3) {
        if (this->cardsRemainingInDeck() <= questionRound && activePlayer_ == 0) {
            Color pile_color = this->generatePileQuestion();
            //Look at the current size of the pile
            int pile_score = pileOf(pile_color).size_;
            (*log_) << "What is the current score of the " 
                    << colorname(pile_color) << " pile?" << "\n";
            (*log_) << "answer: " << pile_score << "\n";
            //Log different variables for dataset analysis later on
            (*log_) << "cards_remaining: " << questionRound << "\n";
            (*log_) << "question_pile: " << colorname(pile_color) << "\n";

            // End the game after generating the question and logging the answer
            break;
        }
    //In-game Q&A logic for discards
    } else if (this->qa_ == 4) {
        if (this->cardsRemainingInDeck() <= questionRound && activePlayer_ == 0) {
            Card discard_card = this->generateDiscardQuestion();
            //Look at the discards of the card
            int num_discards = std::count(discards_.begin(), discards_.end(), discard_card);
            (*log_) << "How many " 
                    << int(discard_card.value) << colorname(discard_card.color)
                    << " cards have been discarded?" << "\n";
            (*log_) << "answer: " << num_discards << "\n";
            //Log different variables for dataset analysis later on
            (*log_) << "cards_remaining: " << questionRound << "\n";

            // End the game after generating the question and logging the answer
            break;
        }
    //In-game Q&A logic for cards remaining in deck
    } else if (this->qa_ == 5) {
        if (this->cardsRemainingInDeck() <= questionRound && activePlayer_ == 0) {
            (*log_) << "How many cards are currently remaining in the deck?" << "\n";
            (*log_) << "answer: " << this->cardsRemainingInDeck() << "\n";

            // End the game after generating the question and logging the answer
            break;
        }
    }

    players_[activePlayer_]->pleaseMakeMove(*this);  /* make a move */
    //(*log_) << moveExplanation << "\n";
    
    // added this short-circuit in case you forcibly end the game, toa void asserts and waiting
    if (this->gameOver()) break;
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ != 0, "bot failed to respond to pleaseMove()");
    assert(movesFromActivePlayer_ == 1);
    movesFromActivePlayer_ = -1;
    for (int i=0; i < numPlayers_; ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveAfterMove(*this);
    }
    activePlayer_ = (activePlayer_ + 1) % numPlayers_;
    assert(0 <= finalCountdown_ && finalCountdown_ <= numPlayers_);
    if (deck_.empty()) {
        if (finalCountdown_ == 0) {
            (*log_) << "0 Cards Remaining\n";
        }
        finalCountdown_ += 1;
    }
  }

  return this->currentScore();
}

void Server::endGameByBombingOut() {
  mulligansRemaining_ = 0;
}

int Server::numPlayers() const
{
    return numPlayers_;
}

int Server::handSize() const
{
    return HAND_SIZE_OVERRIDE >= 0 ? HAND_SIZE_OVERRIDE : ((numPlayers_ <= 3) ? 5 : 4);
}

int Server::whoAmI() const
{
    assert(0 <= observingPlayer_ && observingPlayer_ < numPlayers_);
    return observingPlayer_;
}

int Server::activePlayer() const
{
    return activePlayer_;
}

int Server::sizeOfHandOfPlayer(int player) const
{
    HANABI_SERVER_ASSERT(0 <= player && player < numPlayers_, "player index out of bounds");
    return hands_[player].size();
}

const std::vector<Card>& Server::handOfPlayer(int player) const
{
    HANABI_SERVER_ASSERT(player != observingPlayer_, "cannot observe own hand");
    HANABI_SERVER_ASSERT(0 <= player && player < numPlayers_, "player index out of bounds");
    return hands_[player];
}

const std::vector<int> Server::cardIdsOfHandOfPlayer(int player) const
{
    std::vector<int> res;
    for (auto &card: hands_[player]) {
#ifdef CARD_ID
      res.push_back(card.id);
#else
      res.push_back(0);
#endif
    }
    return res;
}

Card Server::activeCard() const
{
    HANABI_SERVER_ASSERT(activeCardIsObservable_, "called activeCard() from the wrong observer");
    return activeCard_;
}

Pile Server::pileOf(Color color) const
{
    int index = (int)color;
    HANABI_SERVER_ASSERT(0 <= index && index < NUMCOLORS, "invalid Color");
    return piles_[color];
}

const std::vector<Card>& Server::discards() const
{
    return discards_;
}

int Server::hintStonesUsed() const
{
    assert(hintStonesRemaining_ <= NUMHINTS);
    return (NUMHINTS - hintStonesRemaining_);
}

int Server::hintStonesRemaining() const
{
    assert(hintStonesRemaining_ <= NUMHINTS);
    return hintStonesRemaining_;
}

bool Server::discardingIsAllowed() const
{
#ifdef HANABI_ALLOW_DISCARDING_EVEN_WITH_ALL_HINT_STONES
    return true;
#else
    return (hintStonesRemaining_ != NUMHINTS);
#endif
}

int Server::mulligansUsed() const
{
    assert(mulligansRemaining_ <= NUMMULLIGANS);
    return (NUMMULLIGANS - mulligansRemaining_);
}

int Server::mulligansRemaining() const
{
    assert(mulligansRemaining_ <= NUMMULLIGANS);
    return mulligansRemaining_;
}

int Server::cardsRemainingInDeck() const
{
    return deck_.size();
}

int Server::finalCountdown() const
{
  return finalCountdown_;
}

 
AnswerType Server::checkHints(Question question) const{
    AnswerType answer = AnswerType::MAYBE;

    for (const auto& hint : hints_) {
        if (hint.getIsValuable()) {
            if (hint.getReceiverId() == question.getPlayerId() && hint.getCardPosition() == question.getCardPosition()) {
                //Check if positive color hint matches question
                if (question.getType() == Question::Type::COLOR && !hint.getNegativeHint() && hint.getType() == ServerHint::Type::COLOR) {
                    if (hint.getColor() == question.getColor()) {
                        answer = AnswerType::YES;
                    } else {
                        answer = AnswerType::NO;
                    }
                //Check if negative color hint matches question
                } else if (question.getType() == Question::Type::COLOR && hint.getNegativeHint() && hint.getType() == ServerHint::Type::COLOR) {
                    if (hint.getColor() == question.getColor()) {
                        answer = AnswerType::NO;
                    } else {
                        answer = AnswerType::MAYBE;
                    }
                //Check if positive value hint matches question
                } else if (question.getType() == Question::Type::NUMBER && !hint.getNegativeHint() && hint.getType() == ServerHint::Type::NUMBER) {
                    if (hint.getNumber() == question.getNumber()) {
                        answer = AnswerType::YES;
                    } else {
                        answer = AnswerType::NO;
                    }
                //Check if negative value hint matches question
                } else if (question.getType() == Question::Type::NUMBER && hint.getNegativeHint() && hint.getType() == ServerHint::Type::NUMBER) {
                    if (hint.getNumber() == question.getNumber()) {
                        answer = AnswerType::NO;
                    } else {
                        answer = AnswerType::MAYBE;
                    }
                }
            }
        } 
         //If the answer is maybe, keep going through the rest of the hints to look for anything that can help to answer the question with certainty (yes or no)
        if (answer != AnswerType::MAYBE) {
            return answer;
        } 
    }
    return answer;
}
   

AnswerType Server::checkGameState(Question question) const{
    int count = 0;
    int total = 0;

    if (question.getType() == Question::Type::COLOR) {
        Color color = question.getColor();
        total = 10;
        // Check other players' hands
        for (int player = 0; player < numPlayers(); ++player) {
            if (player != question.getPlayerId()) {
                const auto& hand = cheatGetHand(player);
                for (const auto& card : hand) {
                    if (card.color == color) {
                        ++count;
                    }
                }
            }
        }
        // Check discard pile
        for (const auto& card : discards_) {
            if (card.color == color) {
                ++count;
            }
        }
        // Check score piles
        for (int pileIndex = 0; pileIndex < NUMCOLORS; ++pileIndex) {
            if (piles_[pileIndex].color == color) {
                count += piles_[pileIndex].size();
                break;
            }
        }
        // Check player's hints
        for (const auto& hint : hints_) {
            if (hint.getIsValuable()) {
                //Only count when it's a hint about a different card than the one asked in the question
                if (hint.getReceiverId() == question.getPlayerId() && hint.getType() == ServerHint::Type::COLOR && !hint.getNegativeHint() && hint.getColor() == color && hint.getCardPosition() != question.getCardPosition()) {
                    ++count;
                }
            }
        }

    } else if (question.getType() == Question::Type::NUMBER) {
        int value = question.getNumber();
        total = countValues(value);
        // Check other players' hands
        for (int player = 0; player < numPlayers(); ++player) {
            if (player != question.getPlayerId()) {
                const auto& hand = cheatGetHand(player);
                for (const auto& card : hand) {
                    if (card.value == value) {
                        ++count;
                    }
                }
            }
        }
        // Check discard pile
        for (const auto& card : discards_) {
            if (card.value == value) {
                ++count;
            }
        }
        // Check score piles
        for (int pileIndex = 0; pileIndex < NUMCOLORS; ++pileIndex) {
            if (piles_[pileIndex].contains(static_cast<int>(value))) {
                ++count;
            }
        }
        // Check player's hints
        for (const auto& hint : hints_) {
            if (hint.getIsValuable()) {
                //Only count when it's a hint about a different card than the one asked in the question
                if (hint.getReceiverId() == question.getPlayerId() && hint.getType() == ServerHint::Type::NUMBER && !hint.getNegativeHint() && hint.getNumber() == value && hint.getCardPosition() != question.getCardPosition()) {
                    ++count;
                }
            }
            
        }
    }

    if (count == total) {
        return AnswerType::NO;
    } else {
        return AnswerType::MAYBE;
    }
}

Answer Server::processQuestion(Question question) const{
    AnswerType result = checkHints(question);
    //Only check game state when no definitive answer has been found
    if (result == AnswerType::MAYBE) {
        result = checkGameState(question);
    }
    return Answer(result);
}

void Server::pleaseDiscard(int index)
{
    assert(0 <= activePlayer_ && activePlayer_ < numPlayers_);
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ < 1, "bot attempted to move twice");
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ == 0, "called pleaseDiscard() from the wrong observer");
    HANABI_SERVER_ASSERT(0 <= index && index <= hands_[activePlayer_].size(), "invalid card index");
    HANABI_SERVER_ASSERT(discardingIsAllowed(), "all hint stones are already available");

    Card discardedCard = hands_[activePlayer_][index];
    activeCard_ = discardedCard;
    activeCardIsObservable_ = true;

    /* Notify all the players of the discard (before it happens). */
    movesFromActivePlayer_ = -1;
    int oldObservingPlayer = observingPlayer_;
    for (int i=0; i < numPlayers_; ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveBeforeDiscard(*this, activePlayer_, index);
    }
    observingPlayer_ = oldObservingPlayer;
    activeCardIsObservable_ = false;

    /* Discard the selected card. */
    discards_.push_back(discardedCard);

    if (log_) {
        if (activePlayer_ == 0) {
            (*log_) << "You" 
                << " X " << nth(index, hands_[activePlayer_].size())
                << " (" << discardedCard.toString() << "). ";
        } else {
            (*log_) << "P" << activePlayer_
                << " X " << nth(index, hands_[activePlayer_].size())
                << " (" << discardedCard.toString() << "). ";
        }
    }

    /* Shift the old cards down, and draw a replacement if possible. */
    hands_[activePlayer_].erase(hands_[activePlayer_].begin() + index);

    if (mulligansRemaining_ > 0 && !deck_.empty()) {
        Card replacementCard = this->draw_();
        hands_[activePlayer_].push_back(replacementCard);
        if (log_) {
            if (activePlayer_ == 0) {
                (*log_) << "You D card\n";
            } else {
                (*log_) << "P" << activePlayer_
                    << " D " << replacementCard.toString() << "\n";
            }   
        }
    }

    regainHintStoneIfPossible_();
    movesFromActivePlayer_ = 1;

    //Hint logic. It only affects the hints vector so it does not matter where in pleaseDiscard() it is updated
    this->pleaseUpdateValuableHintsAfterPlay(index);
    this->pleaseUpdateHintCardPosition(index);
}

void Server::pleasePlay(int index)
{
    assert(0 <= activePlayer_ && activePlayer_ < hands_.size());
    assert(players_.size() == hands_.size());
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ < 1, "bot attempted to move twice");
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ == 0, "called pleasePlay() from the wrong observer");
    HANABI_SERVER_ASSERT(0 <= index && index <= hands_[activePlayer_].size(), "invalid card index");

    Card selectedCard = hands_[activePlayer_][index];
    activeCard_ = selectedCard;
    activeCardIsObservable_ = true;

    /* Notify all the players of the attempted play (before it happens). */
    movesFromActivePlayer_ = -1;
    int oldObservingPlayer = observingPlayer_;
    for (int i=0; i < players_.size(); ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveBeforePlay(*this, activePlayer_, index);
    }
    observingPlayer_ = oldObservingPlayer;
    activeCardIsObservable_ = false;

    /* Examine the selected card. */
    Pile &pile = piles_[(int)selectedCard.color];

    if (pile.nextValueIs(selectedCard.value)) {
        if (log_) {
            if (activePlayer_ == 0) {
                (*log_) << "You P " << nth(index, hands_[activePlayer_].size())
                    << " (" << selectedCard.toString() << "). ";
            } else {
                (*log_) << "P" << activePlayer_
                    << " P " << nth(index, hands_[activePlayer_].size())
                    << " (" << selectedCard.toString() << "). ";
            }
        }
        pile.increment_();
        if (selectedCard.value == 5) {
            /* Successfully playing a 5 regains a hint stone. */
            regainHintStoneIfPossible_();
        }
    } else {
        /* The card was unplayable! */
        if (log_) {
            if (activePlayer_ == 0) {
                (*log_) << "You P " << nth(index, hands_[activePlayer_].size())
                    << " (" << selectedCard.toString() << ")"
                    << " but failed. ";
            } else {
                (*log_) << "P" << activePlayer_
                    << " P " << nth(index, hands_[activePlayer_].size())
                    << " (" << selectedCard.toString() << ")"
                    << " but failed. ";
            }
        }
        discards_.push_back(selectedCard);
        loseMulligan_();
    }

    /* Shift the old cards down, and draw a replacement if possible. */
    hands_[activePlayer_].erase(hands_[activePlayer_].begin() + index);

    if (mulligansRemaining_ > 0 && !deck_.empty()) {
        Card replacementCard = this->draw_();
        hands_[activePlayer_].push_back(replacementCard);
        if (log_) {
            if (activePlayer_ == 0) {
                (*log_) << "You D card\n";
            } else {
                (*log_) << "P" << activePlayer_
                    << " D " << replacementCard.toString() << "\n";
            }   
        }
    }

    this->logPiles_();

    movesFromActivePlayer_ = 1;

    //Hint logic. It only affects the hints vector so it does not matter where in pleasePlay() it is updated
    this->pleaseUpdateValuableHintsAfterPlay(index);
    this->pleaseUpdateHintCardPosition(index);
}

void Server::pleaseGiveColorHint(int to, Color color)
{
    assert(0 <= activePlayer_ && activePlayer_ < hands_.size());
    assert(players_.size() == hands_.size());
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ < 1, "bot attempted to move twice");
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ == 0, "called pleaseGiveColorHint() from the wrong observer");
    HANABI_SERVER_ASSERT(0 <= to && to < hands_.size(), "invalid player index");
    HANABI_SERVER_ASSERT(RED <= color && color <= BLUE, "invalid color");
    HANABI_SERVER_ASSERT(hintStonesRemaining_ != 0, "no hint stones remaining");
    HANABI_SERVER_ASSERT(to != activePlayer_, "cannot give hint to oneself");

    CardIndices card_indices;
    for (int i=0; i < hands_[to].size(); ++i) {
        if (hands_[to][i].color == color) {
            card_indices.add(i);
        }
    }
#ifndef HANABI_ALLOW_EMPTY_HINTS
    HANABI_SERVER_ASSERT(!card_indices.empty(), "hint must include at least one card");
#endif

    if (log_) {
        const bool singular = (card_indices.size() == 1);
        if (activePlayer_ == 0) {
            // LLM player gives hint
            (*log_) << "You T P" << to;
        } else if (to == 0) {
            // Hint given to LLM player
            (*log_) << "P" << activePlayer_ << " T You";
        } else {
            // Hint between other players
            (*log_) << "P" << activePlayer_ << " T P" << to;
        }
        
        if (card_indices.empty()) {
            (*log_) << " no";
        } else {
            (*log_) << (card_indices.size() == hands_[to].size() ? " all" : (" " + nth(card_indices, hands_[to].size())));
        }
        
        (*log_) << (singular ? " card is " : " cards are ") << colorname(color) << "\n"; 
    }

    //Hint logic. Given hint should be added to the server hint vector
    if (card_indices.empty()) {
        for (int i=0; i < sizeOfHandOfPlayer(activePlayer_); ++i) {
            this->pleaseAddColorHint(activePlayer_, to, i, true, color);
        }
    } else {
        for (int i = 0; i < card_indices.size(); ++i) {
            this->pleaseAddColorHint(activePlayer_, to, card_indices[i], false, color);
        }
    }

    /* Notify all the players of the given hint. */
    movesFromActivePlayer_ = -1;
    int oldObservingPlayer = observingPlayer_;
    for (int i=0; i < players_.size(); ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveColorHint(*this, activePlayer_, to, color, card_indices);
    }
    observingPlayer_ = oldObservingPlayer;

    hintStonesRemaining_ -= 1;
    movesFromActivePlayer_ = 1;
}

void Server::pleaseGiveValueHint(int to, Value value)
{
    assert(0 <= activePlayer_ && activePlayer_ < hands_.size());
    assert(players_.size() == hands_.size());
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ < 1, "bot attempted to move twice");
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ == 0, "called pleaseGiveValueHint() from the wrong observer");
    HANABI_SERVER_ASSERT(0 <= to && to < hands_.size(), "invalid player index");
    HANABI_SERVER_ASSERT(1 <= value && value <= 5, "invalid value");
    HANABI_SERVER_ASSERT(hintStonesRemaining_ != 0, "no hint stones remaining");
    HANABI_SERVER_ASSERT(to != activePlayer_, "cannot give hint to oneself");

    CardIndices card_indices;
    for (int i=0; i < hands_[to].size(); ++i) {
        if (hands_[to][i].value == value) {
            card_indices.add(i);
        }
    }
#ifndef HANABI_ALLOW_EMPTY_HINTS
    HANABI_SERVER_ASSERT(!card_indices.empty(), "hint must include at least one card");
#endif

    if (log_) {
        const bool singular = (card_indices.size() == 1);
        if (activePlayer_ == 0) {
            // LLM player gives hint
            (*log_) << "You T P" << to;
        } else if (to == 0) {
            // Hint given to LLM player
            (*log_) << "P" << activePlayer_ << " T You";
        } else {
            // Hint between other players
            (*log_) << "P" << activePlayer_ << " T P" << to;
        }
        
        if (card_indices.empty()) {
            (*log_) << " no";
        } else {
            (*log_) << (card_indices.size() == hands_[to].size() ? " all" : (" " + nth(card_indices, hands_[to].size())));
        }
        
        /*Cast value to integer for logging purposes*/
        (*log_) << (singular ? " card is " : " cards are ") << static_cast<int>(value) << "\n";
    }

    //Hint logic. Given hint should be added to the server hint vector
    if (card_indices.empty()) {
        for (int i=0; i < sizeOfHandOfPlayer(activePlayer_); ++i) {
            this->pleaseAddValueHint(activePlayer_, to, i, true, static_cast<int>(value));
        }
    } else {
        for (int i = 0; i < card_indices.size(); ++i) {
            this->pleaseAddValueHint(activePlayer_, to, card_indices[i], false, static_cast<int>(value));
        }
    }

    /* Notify all the players of the given hint. */
    movesFromActivePlayer_ = -1;
    int oldObservingPlayer = observingPlayer_;
    for (int i=0; i < players_.size(); ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveValueHint(*this, activePlayer_, to, value, card_indices);
    }
    observingPlayer_ = oldObservingPlayer;

    hintStonesRemaining_ -= 1;
    movesFromActivePlayer_ = 1;
}

void Server::pleaseAddColorHint(int giverId, int receiverId, int cardPosition, bool negativeHint, Color color) {
    ServerHint newHint(giverId, receiverId, cardPosition, negativeHint, color);
    hints_.push_back(newHint);
}

void Server::pleaseAddValueHint(int giverId, int receiverId, int cardPosition, bool negativeHint, int number) {
    ServerHint newHint(giverId, receiverId, cardPosition, negativeHint, number);
    hints_.push_back(newHint);
}

//If a hint is given, it is added to the vector of hints as a valuable hint
//If a card is played or discarded, two things happen: 1. Hint is not valuable anymore (negative or not). 2. Valuable hints about cards to the right of that card need to change its index (index - 1)
void Server::pleaseUpdateValuableHints() {
    for (auto& hint : hints_) {
        //Only check valuable hints. Once a hint is not valuable, it should not be considered anymore
        if (hint.getIsValuable() == true) {
            int playerId = hint.getReceiverId();
            int cardPosition = hint.getCardPosition();
            const std::vector<Card>& playerHand = cheatGetHand(playerId);

            // If the hint card position is out of bounds, mark it as not valuable
            if (cardPosition >= playerHand.size()) {
                hint.setIsValuable(false);
                continue;
            }

            // Check if the hint still aligns with the hint information
            const Card& card = playerHand[cardPosition];
            if (hint.getType() == ServerHint::Type::COLOR && !hint.getNegativeHint() && card.color == hint.getColor()) {
                continue;
            } else if (hint.getType() == ServerHint::Type::COLOR && hint.getNegativeHint() && card.color != hint.getColor()) {
                continue;
            } else if (hint.getType() == ServerHint::Type::NUMBER && !hint.getNegativeHint() && card.value == hint.getNumber()) {
                continue;
            } else if (hint.getType() == ServerHint::Type::NUMBER && hint.getNegativeHint() && card.value != hint.getNumber()) {
                continue;
            } else {
                hint.setIsValuable(false);
            }
        }
    }
}

void Server::pleaseUpdateValuableHintsAfterPlay(int index) {
    for (auto& hint : hints_) {
        //Only look for valuable hints of active player in the position of the card played-discarded, no matter the color or the value
        if (hint.getIsValuable() == true && hint.getReceiverId() == activePlayer() && hint.getCardPosition() == index) {
            hint.setIsValuable(false);
        }
    }
}

void Server::pleaseUpdateHintCardPosition(int index) {
    for (auto& hint : hints_) {
        if (hint.getReceiverId() == activePlayer() && hint.getCardPosition() > index) {
            hint.setCardPosition(hint.getCardPosition() - 1);
        }
    }
}

int Server::selectQuestionRound() {
    std::mt19937 rng(std::random_device{}());

    int first_question_round = 50 - numPlayers_ * handSize();
    int last_question_round = numPlayers_ - 1;
    std::uniform_int_distribution<int> remainingCardDist(last_question_round, first_question_round);

    return remainingCardDist(rng);
}

Question Server::generateRandomQuestion() {
    std::mt19937 rng(std::random_device{}());

    std::uniform_int_distribution<int> typeDist(0, 1);
    std::uniform_int_distribution<int> colorDist(0, NUMCOLORS - 1);
    std::uniform_int_distribution<int> valueDist(1, VALUE_MAX);
    std::uniform_int_distribution<int> positionDist(0, this->handSize() - 1);

    int type = typeDist(rng);
    int position = positionDist(rng);

    if (type == 0) {
        Color color = static_cast<Color>(colorDist(rng));
        return Question(this->activePlayer_, position, color);
    } else {
        Value value = static_cast<Value>(valueDist(rng));
        return Question(this->activePlayer_, position, value);
    }
}

Color Server::generatePileQuestion() {
    std::mt19937 rng(std::random_device{}());

    std::uniform_int_distribution<int> colorDist(0, NUMCOLORS - 1);
    return static_cast<Color>(colorDist(rng));
}

Card Server::generateDiscardQuestion() {
    std::mt19937 rng(std::random_device{}());

    std::uniform_int_distribution<int> colorDist(0, NUMCOLORS - 1);
    Color color = static_cast<Color>(colorDist(rng));
    std::uniform_int_distribution<int> valueDist(1, VALUE_MAX);
    Value value = static_cast<Value>(valueDist(rng));

    Card card = Card(color, value);
    return card;
}

void Server::regainHintStoneIfPossible_()
{
    if (hintStonesRemaining_ < NUMHINTS) {
        ++hintStonesRemaining_;
    }
}

void Server::loseMulligan_() {
    --mulligansRemaining_;
    assert(mulligansRemaining_ >= 0);
}


Card Server::draw_()
{
    assert(!deck_.empty());
    Card result = deck_.back();
    deck_.pop_back();
    return result;
}

std::string Server::discardsAsString() const
{
    if (discards_.size() == 0) {
      return "";
    }
    std::ostringstream oss;
    for (const Card &card : discards_) {
        oss << ' ' << card.toString();
    }
    return oss.str().substr(1);
}

std::string Server::handsAsString() const
{
    std::ostringstream oss;
    for (int i=0; i < numPlayers_; ++i) {
        for (int j=0; j < (int)hands_[i].size(); ++j) {
            oss << (j ? ',' : ' ') << hands_[i][j].toString();
        }
    }
    return oss.str().substr(1);
}

std::string Server::handsAsStringWithoutPlayer0() const
{
    std::ostringstream oss;
    for (int i=1; i < numPlayers_; ++i) {
        for (int j=0; j < (int)hands_[i].size(); ++j) {
            oss << (j ? ',' : ' ') << hands_[i][j].toString();
        }
    }
    return oss.str().substr(1);
}

std::string Server::pilesAsString() const
{
    std::ostringstream oss;
    for (Color k = RED; k <= BLUE; ++k) {
        oss << ' ' << piles_[k].size() << Card(k, 1).toString()[1];
    }
    return oss.str().substr(1);
}

std::vector<Card> Server::cheatGetHand(int index) const
{
  return hands_[index];
}

int Server::countValues(int value) const
{
    switch (value) {
        case 1: return 15;
        case 2: case 3: case 4: return 10;
        case 5: return 5;
        default: HANABI_SERVER_ASSERT(false, "invalid card value");
    }
    return -1; // silence compiler
}

void Server::logHands_() const
{
    if (log_) {
        (*log_) << "Hands:";
        for (int i=1; i < numPlayers_; ++i) {
            (*log_) << " P" << i << " cards";
            for (int j=0; j < (int)hands_[i].size(); ++j) {
                (*log_) << (j ? "," : " ") << hands_[i][j].toString();
            }
            (*log_) << ";";
        }
        (*log_) << "\n";
    }
}

void Server::logPiles_() const
{
    if (log_) {
        (*log_) << "Piles:";
        for (Color k = RED; k <= BLUE; ++k) {
            (*log_) << " " << piles_[k].size() << Card(k, 1).toString()[1];
        }
        (*log_) << "\n";
    }
}

std::map<std::string, std::shared_ptr<BotFactory>> &getBotFactoryMap() {
  static std::map<std::string, std::shared_ptr<BotFactory>> factoryMap;
  return factoryMap;
}

void registerBotFactory(std::string name, std::shared_ptr<Hanabi::BotFactory> factory) {
  getBotFactoryMap()[name] = factory;
  std::cerr << "Registered " << name << std::endl;
}

std::shared_ptr<Hanabi::BotFactory> getBotFactory(const std::string &botName) {
  if (getBotFactoryMap().count(botName) == 0) {
    throw std::runtime_error("Unknown bot: " + botName);
  }
  return getBotFactoryMap().at(botName);
}

// Constructor for color question
Question::Question(int playerId, int cardPosition, Color color)
    : type(Type::COLOR), playerId(playerId), cardPosition(cardPosition), color(color) {}

// Constructor for number question
Question::Question(int playerId, int cardPosition, int number)
    : type(Type::NUMBER), playerId(playerId), cardPosition(cardPosition), number(number) {}


// Getter for QuestionType
Question::Type Question::getType() const {
    // Return the type of question
    return type;
}

// Getter for playerId
int Question::getPlayerId() const {
    // Return the ID of the player who is meant to answer the question
    return playerId;
}

// Getter for cardPosition
int Question::getCardPosition() const {
    // Return the card position
    return cardPosition;
}

Color Question::getColor() const {
    assert(type == Type::COLOR);
    return color;
}

int Question::getNumber() const {
    assert(type == Type::NUMBER);
    return number;
}

// Constructor for color hint
ServerHint::ServerHint(int giverId, int receiverId, int cardPosition, bool negativeHint, Color color, bool isValuable)
    : type(Type::COLOR), giverId(giverId), receiverId(receiverId), cardPosition(cardPosition), negativeHint(negativeHint), color(color), isValuable(isValuable) {}

// Constructor for value hint
ServerHint::ServerHint(int giverId, int receiverId, int cardPosition, bool negativeHint, int number, bool isValuable)
    : type(Type::NUMBER), giverId(giverId), receiverId(receiverId), cardPosition(cardPosition), negativeHint(negativeHint), number(number), isValuable(isValuable) {}

ServerHint::Type ServerHint::getType() const {
    // Return the type of hint
    return type;
}

int ServerHint::getGiverId() const {
    return giverId;
}

int ServerHint::getReceiverId() const {
    return receiverId;
}

int ServerHint::getCardPosition() const {
    return cardPosition;
}

void ServerHint::setCardPosition(int newPosition) {
    cardPosition = newPosition;
}

bool ServerHint::getNegativeHint() const {
    return negativeHint;
}

Color ServerHint::getColor() const {
    assert(type == Type::COLOR);
    return color;
}  

int ServerHint::getNumber() const {
    assert(type == Type::NUMBER);
    return number;
}

bool ServerHint::getIsValuable() const {
    return isValuable;
}

void ServerHint::setIsValuable(bool isValuable) {
    this->isValuable = isValuable;
}

//Constructors for Answer class
Answer::Answer(AnswerType answer)
    : answer(answer) { }

AnswerType Answer::getType() const {
    return answer;
}

std::string Answer::answerAsString() const {
    switch (answer) {
        case AnswerType::NO: return "No";
        case AnswerType::YES: return "Yes";
        case AnswerType::MAYBE: return "Maybe";
        default: return "Unknown";
    }
}

}  /* namespace Hanabi */
