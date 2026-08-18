function on_trigger_enter(other)
  log("goal reached by " .. tostring(other))
  publish("level.complete", "corridor")
  ui_set_text("msg", "Done")
end
