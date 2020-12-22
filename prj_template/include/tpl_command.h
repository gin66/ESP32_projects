
enum Command {
  CmdIdle,
  CmdDeepSleep,
#ifdef IS_ESP32CAM
  CmdFlash,
  CmdSendJpg2Ws,

  // either take photo or send photo referenced by tpl_config.curr_jpg
  CmdSendJpg2Bot,
  CmdWaitSendJpg2Bot,
#endif
};

extern enum Command tpl_command;

void tpl_command_setup(void (*func)(enum Command command));
