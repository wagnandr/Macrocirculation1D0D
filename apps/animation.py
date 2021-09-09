import json
import numpy as np
import argparse
import os
from matplotlib import pyplot as plt
from matplotlib.animation import FuncAnimation
plt.style.use('seaborn-pastel')


parser = argparse.ArgumentParser(description='Animator for the vessel data.')
parser.add_argument('--vessels', type=int, nargs='+', help='A list of ids of the vessels to plot.', default=[0])
parser.add_argument('--filepath', type=str, required=True)
parser.add_argument('--t-start', type=float, default=0)

args = parser.parse_args()

directory = os.path.dirname(args.filepath)

with open(args.filepath) as f:
    meta = json.loads(f.read())

times = np.loadtxt(os.path.join(directory, meta['filepath_time']), delimiter=',')

start_index = int(sum(times < args.t_start)) or 0


def find_vessel(vessel_id):
    for vessel in meta['vessels']:
        if vessel['edge_id'] == vessel_id:
            return vessel
    raise 'vessel with id {} not found'.format(vessel_id)


def load_vessel_component(vessel_id, component):
    vessel = find_vessel(vessel_id)
    data = np.loadtxt(os.path.join(directory, vessel['filepaths'][component]), delimiter=',')
    data = data[start_index:, :]
    return data
    if component == 'c':
        data_a = np.loadtxt(os.path.join(directory, vessel['filepaths']['a']), delimiter=',')
        data_a = data_a[start_index:, :]
        data /= data_a
    return data


def load_grid(vessel_id):
    return find_vessel(vessel_id)['coordinates']


def load_vessel(vessel_id):
    vessel_info = find_vessel(vessel_id)
    data = {}
    data['q'] = load_vessel_component(vessel_id, 'q')
    if 'a' in vessel_info['filepaths']:
        data['a'] = load_vessel_component(vessel_id, 'a')
    if 'p' in vessel_info['filepaths']:
        data['p'] = load_vessel_component(vessel_id, 'p') / 1.333333
    elif 'a' in vessel_info['filepaths']:
        data['p'] = vessel_info['G0'] * (np.sqrt(data['a']/vessel_info['A0']) - 1) / 1.33332
    if 'c' in vessel_info['filepaths']:
        data['c'] = load_vessel_component(vessel_id, 'c')
        data['c/a'] = data['c'] / data['a']
    data['grid'] = load_grid(vessel_id)
    return data


data_sets = [load_vessel(index) for index in list(args.vessels) ]

fig = plt.figure()
fig.tight_layout()
axes = fig.subplots(3, len(data_sets), sharey='row', sharex='col', squeeze=False)

lines = []
t_index = 0 
for dset_index in range(len(data_sets)):
    ax_Q = axes[0, dset_index]
    ax_p = axes[1, dset_index]
    ax_c = axes[2, dset_index]
    data_Q = data_sets[dset_index]['q'][t_index]
    data_p = data_sets[dset_index]['p'][t_index]
    data_c = data_sets[dset_index]['c/a'][t_index]
    grid = data_sets[dset_index]['grid']
    ax_Q.clear()
    l1, = ax_Q.plot(grid, data_Q, label='Q')
    ax_Q.legend()
    ax_Q.grid(True)
    ax_p.clear()
    l2, = ax_p.plot(grid, data_p, label='p')
    ax_p.legend()
    ax_p.grid(True)
    ax_c.clear()
    l3, = ax_c.plot(grid, data_c, label='c/a')
    ax_c.legend()
    ax_c.grid(True)
    lines.append([l1, l2, l3])
fig.suptitle('t={}'.format(times[start_index+t_index]))

for ax_id, vid in enumerate(args.vessels):
    axes[0,ax_id].set_title('vessel {}'.format(vid))

def animate(i):
    t_index = i % len(times[start_index:])
    for dset_index in range(len(data_sets)):
        ax_Q = axes[0, dset_index]
        ax_p = axes[1, dset_index]
        ax_c = axes[2, dset_index]
        data_Q = data_sets[dset_index]['q'][t_index]
        data_p = data_sets[dset_index]['p'][t_index]
        data_c = data_sets[dset_index]['c/a'][t_index]
        lQ = lines[dset_index][0]
        lp = lines[dset_index][1]
        lc = lines[dset_index][2]
        grid = data_sets[dset_index]['grid'] 
        lQ.set_ydata(data_Q)
        lp.set_ydata(data_p)
        lc.set_ydata(data_c)
        ax_Q.relim()
        ax_p.relim()
        ax_c.set_ylim([0, 1.05])
        ax_Q.autoscale_view()
        ax_p.autoscale_view()
        ax_c.autoscale_view()
    fig.suptitle('t={}'.format(times[start_index+t_index]))

num_frames = len(times[start_index:])

anim = FuncAnimation(fig, animate, interval=20)

is_running = True 
def toggle_animation(event):
    global is_running
    if is_running:
        anim.event_source.stop()
    else:
        anim.event_source.start()
    is_running = not is_running

fig.canvas.mpl_connect('button_press_event', toggle_animation)
                            
plt.show()


anim.save('sine_wave.gif', writer='imagemagick')
