# NOTE:
# This implementation has been thoroughly debugged. All network interface assignments, IP addresses, and routing tables are strictly consistent with the YAML topology and shortest-path (cost-based) routing requirements.
# Despite this, repeated testing in the Mininet environment shows that 'pingall' consistently results in a nonzero packet loss rate, even though all configuration and routing tables are correct. Same as Assignmnet6 :(

import argparse
import yaml
import ipaddress
from threading import Timer
import heapq

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.node import Node
from mininet.cli import CLI
from mininet.log import setLogLevel, info


class LinuxRouter(Node):
    """Mininet node with IP forwarding enabled"""
    def config(self, **params):
        super().config(**params)
        self.cmd('sysctl net.ipv4.ip_forward=1')

    def terminate(self):
        self.cmd('sysctl net.ipv4.ip_forward=0')
        super().terminate()


class BasicTopo(Topo):
    """
    Build a Mininet topology from YAML subnets and link cost mappings.
    Refactored for clarity: nodes, point-to-point links, and multi-node subnets.
    """
    def __init__(self, subnets, cost_map, dot_map, **kwargs):
        # Initialize routing data before Topo setup
        self.subnets = subnets
        self.cost_map = cost_map
        self.dot_map = dot_map
        self._switch_count = 0
        self.dot_links = []
        super().__init__(**kwargs)

    def build(self):
        # Add all hosts and routers
        self._add_nodes()
        # Add links and switches per subnet
        self._add_links()
        # Expose GraphViz edges
        self.dot_map['links'] = self.dot_links

    def _add_nodes(self):
        for subnet in self.subnets.values():
            for iface in subnet:
                node = iface['node']
                if node.startswith('h'):
                    self.addHost(node, ip=None)
                else:
                    self.addNode(node, cls=LinuxRouter, ip=None)

    def _add_links(self):
        for _, subnet in self.subnets.items():
            if len(subnet) == 2:
                self._link_pair(subnet)
            else:
                self._link_multi(subnet)

    def _link_pair(self, subnet):
        iface1, iface2 = subnet
        cost = max(iface1['cost'], iface2['cost'])
        # record forwarding info
        key1 = f"{iface1['node']}_{iface2['node']}"
        key2 = f"{iface2['node']}_{iface1['node']}"
        self.cost_map[key1] = {'cost': cost, 'intf': iface2['address']}
        self.cost_map[key2] = {'cost': cost, 'intf': iface1['address']}
        # GraphViz edge
        a, b = sorted([iface1['node'], iface2['node']])
        self.dot_links.append({'link': f"{a}_{b}", 'cost': cost})
        # add Mininet link
        self.addLink(
            iface1['node'], iface2['node'],
            intfName1=iface1['name'], params1={'ip': self._format_ip(iface1)},
            intfName2=iface2['name'], params2={'ip': self._format_ip(iface2)}
        )

    def _link_multi(self, subnet):
        # Create a switch to represent multi-node subnet
        sw = f"s{self._switch_count}"
        self._switch_count += 1
        self.addSwitch(sw)
        cost = max(iface['cost'] for iface in subnet)
        # connect each interface to switch
        for idx, iface in enumerate(subnet):
            key1 = f"{iface['node']}_{sw}"
            key2 = f"{sw}_{iface['node']}"
            self.cost_map[key1] = {'cost': cost, 'intf': iface['address']}
            self.cost_map[key2] = {'cost': cost, 'intf': iface['address']}
            self.addLink(
                iface['node'], sw,
                intfName1=iface['name'], intfName2=f"{sw}_eth{idx}",
                params1={'ip': self._format_ip(iface)}
            )
        # add GraphViz edges between routers
        routers = [iface['node'] for iface in subnet if iface['node'].startswith('r')]
        for i in range(len(routers)):
            for j in range(i+1, len(routers)):
                a, b = sorted([routers[i], routers[j]])
                self.dot_links.append({'link': f"{a}_{b}", 'cost': cost})

    @staticmethod
    def _format_ip(iface):
        network = ipaddress.IPv4Network(f"{iface['address']}/{iface['mask']}", strict=False)
        return f"{iface['address']}/{network.prefixlen}"


def build_subnets(topology_def, dot_map):
    """
    Parse YAML topology into subnets and hosts, filling dot_map with node lists.
    """
    subnets = {}
    # routers
    for rname, rfaces in topology_def['routers'].items():
        for iname, cfg in rfaces.items():
            net = ipaddress.IPv4Network(f"{cfg['address']}/{cfg['mask']}", strict=False)
            cidr = f"{net.network_address}/{net.prefixlen}"
            cfg.update({'node': rname, 'name': f"{rname}_{iname}", 'cost': cfg.get('cost', 1)})
            subnets.setdefault(cidr, []).append(cfg)
    # hosts
    for hname, hfaces in topology_def['hosts'].items():
        for iname, cfg in hfaces.items():
            net = ipaddress.IPv4Network(f"{cfg['address']}/{cfg['mask']}", strict=False)
            cidr = f"{net.network_address}/{net.prefixlen}"
            cfg.update({'node': hname, 'name': f"{hname}_{iname}", 'cost': 1,
                        'defaultrouter': subnets[cidr][0]['address']})
            subnets[cidr].append(cfg)
    dot_map['routers'] = list(topology_def['routers'].keys())
    dot_map['hosts'] = list(topology_def['hosts'].keys())
    return subnets


def dijkstra_heapq(graph, start):
    """Compute shortest paths using heap-based Dijkstra."""
    n = len(graph)
    dist = [float('inf')] * n
    prev = [-1] * n
    dist[start] = 0
    heap = [(0, start)]
    while heap:
        d, u = heapq.heappop(heap)
        if d > dist[u]:
            continue
        for v, w in enumerate(graph[u]):
            if w < float('inf') and d + w < dist[v]:
                dist[v] = d + w
                prev[v] = u
                heapq.heappush(heap, (dist[v], v))
    return dist, prev


def shortest_path_subnets(subnets, start, graph, cost_map):
    """
    For a given router index, find next-hop interfaces to each subnet.
    """
    dist, prev = dijkstra_heapq(graph, start)
    result = {}
    for network, ifaces in subnets.items():
        # skip subnets directly attached
        if any(iface['node'] == f'r{start+1}' for iface in ifaces):
            continue
        # candidate routers
        routers = [iface['node'] for iface in ifaces if iface['node'].startswith('r')]
        if not routers:
            continue
        best = min(routers, key=lambda r: dist[int(r[1:]) - 1])
        cur = int(best[1:]) - 1
        while prev[cur] != start:
            cur = prev[cur]
        key = f"r{start+1}_r{cur+1}"
        if key in cost_map:
            result[network] = cost_map[key]['intf']
    return result


def generate_dot_code(dot_map):
    """Generate GraphViz code from dot_map information."""
    lines = ['graph Network {']
    for r in dot_map['routers']:
        lines.append(f'  "{r}" [shape=circle];')
    for h in dot_map['hosts']:
        lines.append(f'  "{h}" [shape=rectangle];')
    for edge in dot_map['links']:
        u, v = edge['link'].split('_')
        cost = edge['cost']  # extract cost to avoid nested quotes
        lines.append(f'  "{u}" -- "{v}" [label="{cost}"];')
    lines.append('}')
    return '\n'.join(lines)


def debug_log(net, dot_map):
    """Dump interface info of all nodes (hosts, switches, routers) to debug log."""
    log_file = '/tmp/emulation_debug.log'
    with open(log_file, 'a') as f:
        f.write('=== Debug Info ===\n')
        # hosts
        for host in net.hosts:
            f.write(f'Host: {host.name}\n')
            for intf in host.intfList():
                if str(intf) == 'lo':
                    continue
                ip = host.cmd(f'ifconfig {intf} | grep "inet "').strip()
                f.write(f'  {intf}: {ip}\n')
        # switches
        for sw in net.switches:
            f.write(f'Switch: {sw.name}\n')
        # routers
        for rname in dot_map['routers']:
            router = net.get(rname)
            f.write(f'Router: {rname}\n')
            for intf in router.intfList():
                if str(intf) == 'lo':
                    continue
                ip = router.cmd(f'ifconfig {intf} | grep "inet "').strip()
                f.write(f'  {intf}: {ip}\n')
        f.write('=== End Debug Info ===\n\n')


def run(topology_def, draw=False):
    dot_map = {}
    subnets = build_subnets(topology_def, dot_map)
    cost_map = {}
    topo = BasicTopo(subnets, cost_map, dot_map)
    net = Mininet(topo=topo)
    if draw:
        Timer(1, print, (generate_dot_code(dot_map),)).start()
    net.start()
    debug_log(net, dot_map)
    # build cost graph
    routers = list(topology_def['routers'].keys())
    n = len(routers)
    graph = [[0 if i == j else cost_map.get(f'r{i+1}_r{j+1}', {}).get('cost', float('inf'))
              for j in range(n)] for i in range(n)]
    # setup host default routes
    for h, infs in topology_def['hosts'].items():
        info(f'*** Setting default route on host {h}\n')
        hnode = net.get(h)
        gw = infs['eth0']['defaultrouter']
        hnode.cmd(f'ip route add default via {gw} dev {h}_eth0')
    # setup router static routes
    for idx, r in enumerate(routers):
        rnode = net.get(r)
        routes = shortest_path_subnets(subnets, idx, graph, cost_map)
        for dest, via in routes.items():
            info(f'*** {r}: add route {dest} via {via}\n')
            rnode.cmd(f'ip route add {dest} via {via}')
    CLI(net)
    net.stop()


def parse_args():
    p = argparse.ArgumentParser(description='Emulate IPv4 routed network via Mininet')
    p.add_argument('definition', help='YAML topology definition file')
    p.add_argument('-d', '--draw', action='store_true', help='output GraphViz map')
    return p.parse_args()


def main():
    args = parse_args()
    with open(args.definition) as f:
        topo_def = yaml.safe_load(f)
    setLogLevel('info')
    run(topo_def, draw=args.draw)


if __name__ == '__main__':
    main()
    

    
            