--@export bob_amp:number=0.05
t = t or 0
function on_update(dt)
  t = t + dt
  local x, y, z = get_pos()
  if x then
    set_pos(x, 0.5 + bob_amp * math.sin(t * 4), z)
  end
end
