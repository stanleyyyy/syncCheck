#!/usr/bin/env python3
import sys
import os
import re
import glob
import struct
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Button, Slider, CheckButtons
from scipy.signal import butter, filtfilt

###############################################################################
# 1) READING CORRELATION DATA (NEW HEADER FORMAT: 12 bytes)
###############################################################################
def print_help(script_name):
	print(f"""
Usage:
	{script_name} <base_name_prefix>
or:
	{script_name} -h
	{script_name} --help

Examples:
	# If your files are named like correlation_data_2_0_00053.bin, correlation_data_2_0_00054.bin, etc.
	# then you can run:
	{script_name} correlation_data_2_0_

Description:
	This script:
	  1. Searches for all files matching <base_name_prefix>*\\.bin (e.g. "correlation_data_2_0_*.bin").
	  2. Reads each correlation file whose header is 12 bytes:
	       - padded_size (int32)
	       - sample_rate (int32)
	       - interp_factor (int32)
	     followed by float32 correlation data of length padded_size.
	  3. Computes the peak index from the data (using np.argmax on the absolute values).
	  4. Plots:
	       - The raw correlation (blue)
	       - The smoothed envelope of the absolute correlation (green dashed)
	       - The raw correlation peak (red dot)
	       - The envelope’s peak (magenta dot)
	  5. Offers interactive navigation and a simple PSLR-based classification.
""")

def parse_filename(filename):
	basename = os.path.basename(filename)
	pattern = r'^correlation_data_(\d+)_(\d+)_(\d+)\.bin$'
	match = re.match(pattern, basename)
	if match:
		bandA = int(match.group(1))
		bandB = int(match.group(2))
		measurement = int(match.group(3))
		return bandA, bandB, measurement
	return None, None, None

def read_correlation(filename):
	"""
	Reads a correlation file with 12-byte header (padded_size, sample_rate, interp_factor)
	and returns:
	  times, data, peak_idx, peak_delay_ms, padded_size, sample_rate, interp_factor
	The data is assumed to be already shifted so that index padded_size/2 corresponds to 0 delay.
	"""
	with open(filename, 'rb') as f:
		padded_size, sample_rate, interp_factor = struct.unpack('iii', f.read(12))
		data = np.frombuffer(f.read(), dtype=np.float32)
	
	times = (np.arange(padded_size) - padded_size/2) / (sample_rate * interp_factor) * 1000.0
	# Compute peak index from the absolute data (this is our only peak measurement)
	peak_idx = int(np.argmax(np.abs(data)))
	peak_delay_ms = times[peak_idx]
	return times, data, peak_idx, peak_delay_ms, padded_size, sample_rate, interp_factor

###############################################################################
# 2) LOW-PASS FILTERING THE ENVELOPE (PEAK FOLLOWER)
###############################################################################
def get_smooth_envelope(data, times, cutoff_freq=5.0, order=4):
	"""
	Computes the absolute value (envelope) of 'data' and applies a low-pass Butterworth filter.
	Returns the smoothed envelope.
	"""
	abs_data = np.abs(data)
	dt = times[1] - times[0]
	if dt <= 0.0:
		return abs_data
	sample_rate = 1.0 / dt  # if times are in ms, sample_rate is in 1/ms
	nyquist = 0.5 * sample_rate
	normal_cutoff = cutoff_freq / nyquist
	if normal_cutoff >= 1.0:
		return abs_data
	b, a = butter(order, normal_cutoff, btype='low', analog=False)
	env_smooth = filtfilt(b, a, abs_data, method='gust')
	return env_smooth

###############################################################################
# 3) SIMPLE PSLR-BASED CLASSIFICATION (using computed peak)
###############################################################################
def compute_pslr(corr, exclusion_radius=5):
	"""
	Computes PSLR (Peak-to-Side-Lobe Ratio) from the correlation data.
	Finds the main peak (using np.argmax on abs(corr)), then excludes a window of
	size 2*exclusion_radius+1 around the peak. The largest absolute value outside that
	window is considered the side lobe.
	Returns (pslr_linear, pslr_db).
	"""
	abs_corr = np.abs(corr)
	main_idx = int(np.argmax(abs_corr))
	main_peak = abs_corr[main_idx]
	N = len(corr)
	left = max(0, main_idx - exclusion_radius)
	right = min(N, main_idx + exclusion_radius + 1)
	if left < right:
		side_lobes = np.concatenate([abs_corr[:left], abs_corr[right:]])
	else:
		side_lobes = abs_corr
	if len(side_lobes) == 0:
		return float('inf'), float('inf')
	largest_side = np.max(side_lobes)
	if largest_side < 1e-12:
		pslr_linear = float('inf')
	else:
		pslr_linear = main_peak / largest_side
	pslr_db = 20.0 * np.log10(pslr_linear) if np.isfinite(pslr_linear) else float('inf')
	return pslr_linear, pslr_db

def classify_correlation(corr, exclusion_radius=5):
	"""
	Classifies the correlation as "GOOD" if the PSLR (in dB) is at least 10 dB, else "BAD".
	"""
	_, pslr_db = compute_pslr(corr, exclusion_radius)
	print(f"pslr db = {pslr_db} dB")
	return "GOOD" if pslr_db >= 5.0 else "BAD"

###############################################################################
# 4) INTERACTIVE VIEWER
###############################################################################
def get_global_data_range(file_list):
	global_min = float('inf')
	global_max = float('-inf')
	for fname in file_list:
		_, data, _, _, _, _, _ = read_correlation(fname)
		local_min = np.min(data)
		local_max = np.max(data)
		if local_min < global_min:
			global_min = local_min
		if local_max > global_max:
			global_max = local_max
	return global_min, global_max

def update_plot(index):
	global current_index
	current_index = index
	if current_index < 0 or current_index >= len(file_list):
		return

	filename = file_list[current_index]
	times, data, peak_idx, peak_delay_ms, padded_size, sample_rate, interp_factor = read_correlation(filename)
	bandA, bandB, measurement = parse_filename(filename)
	base = os.path.basename(filename)

	# 1) Raw correlation (blue)
	line.set_data(times, data)
	# Mark raw correlation peak (red dot) using computed peak_idx
	peak.set_data(times[peak_idx], data[peak_idx])
	
	# Classification on RAW data
	class_raw = classify_correlation(data, exclusion_radius=500)

	# 2) Smoothed envelope (green dashed)
	env_smooth = get_smooth_envelope(data, times, cutoff_freq=5.0, order=4)
	env_line.set_data(times, env_smooth)
	# Mark envelope's peak (magenta dot)
	env_peak_idx = int(np.argmax(env_smooth))
	env_peak.set_data(times[env_peak_idx], env_smooth[env_peak_idx])
	
	# Classification on FILTERED data
	class_filtered = classify_correlation(env_smooth, exclusion_radius=500)

	# 3) Fix scale if needed
	if fix_scale:
		ax.set_autoscaley_on(False)
		ax.set_ylim(global_min, global_max)
	else:
		ax.set_autoscaley_on(True)
		ax.relim()
		ax.autoscale_view()

	# 4) Update title with file info and classifications
	title_lines = []
	title_lines.append(f"{base}")
	if bandA is not None and bandB is not None and measurement is not None:
		title_lines.append(f"Measurement #{measurement} (Band {bandA} vs Band {bandB})")
	title_lines.append(f"Computed Delay = {peak_delay_ms:.3f} ms")
	title_lines.append(f"Classification (raw) = {class_raw}, (filtered) = {class_filtered}")
	title_lines.append("Blue: Raw, Green Dashed: Smoothed Envelope")
	title_lines.append("Red dot: Raw Peak, Magenta dot: Envelope Peak")
	fig.suptitle("\n".join(title_lines), x=0.5, y=0.97, ha='center', fontsize=11)
	ax.legend()
	fig.canvas.draw_idle()
	print(f"\nLoaded file: {filename}")
	print(f"  Computed peak index = {peak_idx}, Delay = {peak_delay_ms:.3f} ms")
	print(f"  Classification (raw) = {class_raw}, Classification (filtered) = {class_filtered}")

def next_file(event):
	new_index = (current_index + 1) % len(file_list)
	update_plot(new_index)
	index_slider.set_val(new_index)

def prev_file(event):
	new_index = (current_index - 1) % len(file_list)
	update_plot(new_index)
	index_slider.set_val(new_index)

def slider_update(val):
	new_index = int(val)
	update_plot(new_index)

def on_fix_scale(label):
	global fix_scale
	fix_scale = not fix_scale
	update_plot(current_index)

###############################################################################
# MAIN
###############################################################################
if len(sys.argv) != 2:
	if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
		print_help(os.path.basename(sys.argv[0]))
	else:
		print(f"Error: Invalid arguments.\nUse '{os.path.basename(sys.argv[0])} -h' for help.")
	sys.exit(1)
if sys.argv[1] in ("-h", "--help"):
	print_help(os.path.basename(sys.argv[0]))
	sys.exit(0)
base_name = sys.argv[1]
pattern = base_name + "*.bin"
file_list = sorted(glob.glob(pattern))
if not file_list:
	print(f"No correlation data files found matching '{pattern}'. Exiting.")
	sys.exit(1)
global_min, global_max = get_global_data_range(file_list)
fix_scale = False
current_index = 0
times, data, peak_idx, peak_delay_ms, padded_size, sample_rate, interp_factor = read_correlation(file_list[0])
fig, ax = plt.subplots(figsize=(12, 6))
plt.subplots_adjust(left=0.08, right=0.95, bottom=0.3, top=0.82)
line, = ax.plot(times, data, 'b-', label='Raw Correlation')
peak, = ax.plot(times[peak_idx], data[peak_idx], 'ro', label='Raw Peak')
env_line, = ax.plot(times, np.zeros_like(times), 'g--', label='Smoothed Envelope')
env_peak, = ax.plot([times[0]], [0.0], 'mo', label='Envelope Peak')
ax.axvline(x=0, color='k', linestyle='--', alpha=0.3)
ax.set_xlabel('Delay (ms)')
ax.set_ylabel('Correlation')
ax.grid(True)
ax.legend()
axprev = plt.axes([0.15, 0.15, 0.1, 0.05])
axnext = plt.axes([0.75, 0.15, 0.1, 0.05])
bprev = Button(axprev, 'Previous')
bnext = Button(axnext, 'Next')
bprev.on_clicked(prev_file)
bnext.on_clicked(next_file)
slider_ax = plt.axes([0.25, 0.07, 0.5, 0.03])
index_slider = Slider(slider_ax, 'Index', 0, len(file_list) - 1, valinit=current_index, valstep=1, orientation='horizontal')
index_slider.on_changed(slider_update)
check_ax = plt.axes([0.05, 0.02, 0.15, 0.08])
check_button = CheckButtons(check_ax, ["Fix Scale"], [False])
check_button.on_clicked(on_fix_scale)
update_plot(0)
plt.show()
