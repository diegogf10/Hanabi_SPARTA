
#include <cassert>
#include <cstdlib>
#include "Hanabi.h"
#include "BotFactory.h"
#include "BlindBot.h"

using namespace Hanabi;

static void _registerBots() {
  registerBotFactory("BlindBot", std::shared_ptr<Hanabi::BotFactory>(new ::BotFactory<BlindBot>()));
}

static int dummy =  (_registerBots(), 0);

BlindBot::BlindBot(int, int, int) { }
void BlindBot::pleaseObserveBeforeMove(const Server &) { }
void BlindBot::pleaseObserveBeforeDiscard(const Server &, int, int) { }
void BlindBot::pleaseObserveBeforePlay(const Server &, int, int) { }
void BlindBot::pleaseObserveColorHint(const Server &, int, int, Color, CardIndices) { }
void BlindBot::pleaseObserveValueHint(const Server &, int, int, Value, CardIndices) { }
void BlindBot::pleaseObserveAfterMove(const Server &) { }

void BlindBot::pleaseMakeMove(Server &server)
{
    /* Just try to play a random card from my hand. */
    int random_index = std::rand() % server.sizeOfHandOfPlayer(server.whoAmI());
    server.pleasePlay(random_index);
    server.moveExplanation = "The player seems to be playing a random card without considering hints or game state. This strategy will likely result in incorrect moves most of the time, potentially causing loss of life tokens or missed opportunities.";
}
