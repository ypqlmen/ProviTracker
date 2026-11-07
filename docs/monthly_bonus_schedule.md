The app uses its ordinary monthly tiers before the first effective month. A server-managed `monthlyBonusSchedule` array can supply a complete tier list starting with a given calendar month. Each entry has an `effectiveMonth` (YYYY-MM) and an ascending `tiers` array of `threshold` / `ratePerPoint` pairs.

Administrators update `public.provi_user_data.monthly_bonus_schedule`. The authenticated account payload loads only the caller's schedule; old and new clients cannot replace it through ordinary saves. The app refreshes the schedule when it loads the cloud account. Clients before 2.0.2 ignore this field.

A month's points use the rates for the month when the sales were made. The existing payout delay stays in place. A short report spanning a rate change separates points on each side of the change. Reports entirely before the change use the existing calculations.
