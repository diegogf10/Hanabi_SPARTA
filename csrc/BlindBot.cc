
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
}

std::map<std::string, std::string> BlindBot::handKnowledgeToMap() {
    std::map<std::string, std::string> knowledgeMap;
    return knowledgeMap;
}

void BlindBot::printHandKnowledge(const std::map<std::string, std::string>& knowledgeMap) {

}
