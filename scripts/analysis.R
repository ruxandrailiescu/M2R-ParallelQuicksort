#!/usr/bin/env Rscript
# Usage: Rscript scripts/analysis.R data/<dir>/measurements_<HH-MM>.csv
# Prints the tables used in report.md and writes figures next to the CSV.
# Times are right-skewed and positive: means and 95% CIs are computed on log(time)
# (geometric means), and speedups are paired (Seq/Par within the same run).
suppressMessages({library(dplyr); library(tidyr); library(ggplot2)})
f <- commandArgs(TRUE)[1]; out <- sub("\\.csv$", "", f)
d <- read.csv(f)
set.seed(1); B <- 1000

gci <- function(x) { l <- log(x); h <- qt(.975, length(l) - 1) * sd(l) / sqrt(length(l))
  tibble(gm = exp(mean(l)), lo = exp(mean(l) - h), hi = exp(mean(l) + h)) }
pal <- c(`0` = "#8a8984", `2` = "#86b6ef", `4` = "#5598e7", `6` = "#2a78d6", `8` = "#1c5cab", `10` = "#0d366b")
theme_set(theme_minimal(base_size = 11) + theme(panel.grid.minor = element_blank()))
logx <- scale_x_log10(breaks = 10^(2:8), labels = scales::trans_format("log10", scales::math_format(10^.x)))

## 1. Paired speedup Seq/Par per run, and crossover size per level ----------------
sp <- d %>% select(run, size, level, algo, time) %>% pivot_wider(names_from = algo, values_from = time) %>%
  mutate(ls = log(Seq / Par))
ssp <- sp %>% group_by(level, size) %>% reframe(gci(exp(ls)))

cross <- function(sz, ls) {        # size above which Par stays faster (log-log interpolation)
  k <- max(c(0, which(ls < 0)))
  if (k == 0) return(-Inf); if (k == length(sz)) return(Inf)
  10^approx(ls[k:(k + 1)], log10(sz[k:(k + 1)]), xout = 0)$y }
boot <- sp %>% group_by(level, size) %>%            # B bootstrap means of log-speedup per cell
  reframe(b = 1:B, m = replicate(B, mean(sample(ls, replace = TRUE))))
cr <- boot %>% arrange(size) %>% group_by(level, b) %>% summarise(c = cross(size, m), .groups = "drop") %>%
  group_by(level) %>% summarise(lo = quantile(c, .025), hi = quantile(c, .975)) %>%
  left_join(sp %>% group_by(level, size) %>% summarise(m = mean(ls), .groups = "drop") %>% arrange(size) %>%
              group_by(level) %>% summarise(crossover = cross(size, m)), by = "level")
cat("\n== Crossover size (Par faster than Seq for all larger sizes), 95% bootstrap CI ==\n")
print(as.data.frame(cr %>% mutate(across(-level, ~ signif(.x, 2)))))

cat("\n== Best level per size: speedup Seq/Par, geometric mean [95% CI] ==\n")
print(as.data.frame(ssp %>% group_by(size) %>% slice_max(gm, n = 1) %>%
  left_join(ssp %>% filter(level == 10) %>% select(size, L10 = gm), by = "size") %>%
  mutate(across(c(gm, lo, hi, L10), ~ round(.x, 2)))))

## 2. Trend and extrapolation: t = a + b n log2 n, fitted on log(t) for n >= NMIN ----
# Below NMIN the Par overhead depends on n (fewer threads are created when sub-arrays
# run out), so the model is only fitted where the sorting work dominates.
NMIN <- 1e5; NX <- 1e8
groups <- bind_rows(d %>% filter(algo != "Par") %>% mutate(level = NA), d %>% filter(algo == "Par"))
fits <- groups %>% filter(size >= NMIN) %>% group_by(algo, level) %>% group_modify(~ {
  m <- nls(log(time) ~ log(a + b * size * log2(size)), data = .x, start = list(a = 1e-3, b = 1e-9),
           algorithm = "port", lower = c(0, 1e-12))
  ci <- confint.default(m)
  tibble(a = coef(m)[1], b = coef(m)[2], b_lo = ci[2, 1], b_hi = ci[2, 2],
         lack_of_fit = sqrt(mean(tapply(resid(m), .x$size, mean)^2))) }) %>% ungroup()  # RMS of cell-mean log residuals
sq <- fits %>% filter(algo == "Seq")
cat(sprintf("\n== Fit t = a + b n log2 n on n >= %g; speedup b_Seq/b (n -> inf) and predicted at n = %g ==\n", NMIN, NX))
print(as.data.frame(fits %>% mutate(asym = sq$b / b, asym_lo = sq$b / b_hi, asym_hi = sq$b / b_lo,
  pred_1e8 = (sq$a + sq$b * NX * log2(NX)) / (a + b * NX * log2(NX))) %>% select(-b_lo, -b_hi) %>%
  mutate(across(c(a, b, lack_of_fit, starts_with("asym"), pred_1e8), ~ signif(.x, 3)))))

## 3. Sanity checks: algorithm position and temporal drift ------------------------
d <- d %>% group_by(algo, level, size) %>% mutate(res = log(time) - mean(log(time))) %>% ungroup()
cat("\n== Effect of position in the run (vs first), % change in time [95% CI] ==\n")
print(as.data.frame(d %>% group_by(algo) %>% group_modify(~ {
  ci <- confint(lm(res ~ factor(position), data = .x))[-1, ]
  tibble(position = 2:3, pct = 100 * (exp((ci[, 1] + ci[, 2]) / 2) - 1), lo = 100 * (exp(ci[, 1]) - 1),
         hi = 100 * (exp(ci[, 2]) - 1)) }) %>% mutate(across(c(pct, lo, hi), ~ round(.x, 1)))))
cat("\n== Drift: % change in time per 1000 runs [95% CI] ==\n")
print(as.data.frame(d %>% group_by(algo) %>% group_modify(~ {
  ci <- 100 * (exp(1000 * confint(lm(res ~ run, data = .x))[2, ]) - 1)
  tibble(pct = round(mean(ci), 2), lo = round(ci[1], 2), hi = round(ci[2], 2)) })))

## 4. Figures ---------------------------------------------------------------------
st <- bind_rows(d %>% filter(algo != "Par") %>% group_by(algo, size) %>% reframe(gci(time)),
                d %>% filter(algo == "Par") %>% group_by(algo, level, size) %>% reframe(gci(time)))
grid <- expand_grid(size = 10^seq(log10(NMIN), log10(NX), .05), fits %>% select(algo, level, a, b)) %>%
  mutate(t = a + b * size * log2(size))
key <- function(df) mutate(df, key = ifelse(algo == "Par", paste0("Par L", level), algo))
cols <- c(Seq = "#1a1a19", Libc = "#1a1a19", setNames(pal, paste0("Par L", names(pal))))
ltys <- c(Seq = "solid", Libc = "22", setNames(rep("solid", 6), paste0("Par L", names(pal))))
brk <- names(cols)
p1 <- ggplot(key(st), aes(size, gm, colour = key, linetype = key)) +
  annotate("rect", xmin = 1e7, xmax = 1e8, ymin = 0, ymax = Inf, fill = "grey60", alpha = .2) +
  annotate("text", x = 3e7, y = 2e-6, label = "extrapolated", size = 3, colour = "grey40") +
  geom_line(data = key(grid), aes(y = t), linewidth = .4, alpha = .6) +
  geom_errorbar(aes(ymin = lo, ymax = hi), width = .04, linetype = "solid", linewidth = .4) +
  geom_point(aes(shape = key), size = 1.6) +
  scale_shape_manual(values = c(Seq = 16, Libc = 1, setNames(rep(16, 6), paste0("Par L", names(pal)))), breaks = brk, name = NULL) +
  scale_colour_manual(values = cols, breaks = brk, name = NULL) +
  scale_linetype_manual(values = ltys, breaks = brk, name = NULL) +
  logx + scale_y_log10() +
  labs(x = "array size", y = "time (s)",
       subtitle = "Geometric mean +/- 95% CI (points; CIs mostly smaller than marks), 30-180 runs per point.\nLines: a + b*n*log(n) fitted on n >= 1e5.")
suppressWarnings(ggsave(paste0(out, "_time.png"), p1, width = 8, height = 5, dpi = 150, bg = "white"))  # log(0) of the shaded band

p2 <- ggplot(ssp, aes(size, gm, colour = factor(level), fill = factor(level))) +
  geom_hline(yintercept = 1, colour = "grey40", linewidth = .4) +
  geom_ribbon(aes(ymin = lo, ymax = hi), alpha = .2, colour = NA) + geom_line(linewidth = .6) +
  geom_point(size = 1.2) +
  scale_colour_manual(values = pal, name = "thread level") + scale_fill_manual(values = pal, name = "thread level") +
  logx + scale_y_log10(breaks = c(.01, .03, .1, .3, 1, 2, 4)) +
  labs(x = "array size", y = "speedup Seq / Par (log)", subtitle = "Paired per run; geometric mean +/- 95% CI (bands), 30 runs per point")
ggsave(paste0(out, "_speedup.png"), p2, width = 8, height = 4.5, dpi = 150, bg = "white")

p3 <- ggplot(d, aes(run, res)) + geom_point(size = .3, alpha = .25) +
  geom_smooth(method = "loess", formula = y ~ x, colour = "#2a78d6") + facet_wrap(~algo) +
  labs(x = "execution order", y = "log(time) - cell mean", subtitle = "Temporal drift check")
ggsave(paste0(out, "_drift.png"), p3, width = 8, height = 3, dpi = 150, bg = "white")
