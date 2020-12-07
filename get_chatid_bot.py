#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Simple Bot to send timed Telegram messages
# This program is dedicated to the public domain under the CC0 license.
"""
This Bot uses the Updater class to handle the bot and the JobQueue to send
timed messages.

First, a few handler functions are defined. Then, those functions are passed to
the Dispatcher and registered at their respective places.
Then, the bot is started and runs until we press Ctrl-C on the command line.

Usage:
Basic Alarm Bot example, sends a message after a set time.
Press Ctrl-C on the command line or send a signal to the process to stop the
bot.


pip install python-telegram-bot

"""

from telegram.ext import Updater, CommandHandler, Job
import logging
import os
import sys

# Define a few command handlers. These usually take the two arguments bot and
# update. Error handlers also receive the raised TelegramError object in error.
def start_handler(bot, update):
    s = 'Hi!\n'
    print(bot)
    print(update)

def error(bot, update, error):
    logger.warning('Update "%s" caused error "%s"' % (update, error))

def main():
    # Enable logging
    global logger
    logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                        level=logging.INFO)

    logger = logging.getLogger(__name__)

    bots = []

    problog = Updater(sys.argv[1])

    bots.append(problog)

    # Get the dispatcher to register handlers
    for bot in bots:
        dp = bot.dispatcher

        # on different commands - answer in Telegram
        dp.add_handler(CommandHandler("start", start_handler))
        dp.add_handler(CommandHandler("help", start_handler))

        # log all errors
        dp.add_error_handler(error)

        # Start the Bot
        bot.start_polling()

    # Block until the you presses Ctrl-C or the process receives SIGINT,
    # SIGTERM or SIGABRT. This should be used most of the time, since
    # start_polling() is non-blocking and will stop the bot gracefully.
    bots[0].idle()


if __name__ == '__main__':
    main()
