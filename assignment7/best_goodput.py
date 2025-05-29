#!/usr/bin/env python3
"""
Assignment 7: Best Goodput
Standalone implementation: parses a YAML topology+demands file, builds an LP model in CPLEX format,
solves with GLPK, and configures Mininet with MPLS forwarding to maximize the minimum goodput ratio.
"""
import argparse
import yaml
import ipaddress
import subprocess
import tempfile
import re
from collections import deque
from mininet.topo import Topo
from mininet.net import Mininet
from mininet.node import Node
from mininet.cli import CLI
from mininet.log import setLogLevel, info


class LinuxRouter(Node):
    def config(self, **params):
        super().config(**params)
        self.cmd('sysctl -w net.ipv4.ip_forward=1')

    def terminate(self):
        self.cmd('sysctl -w net.ipv4.ip_forward=0')
        super().terminate()


def build_subnets(defn):
    """
    Build subnets mapping: subnet CIDR -> list of endpoints (router/host).
    Following emulation.py pattern with name and defaultrouter fields.
    """
    subnets = {}
    # Routers - exactly like emulation.py
    for rname, rfaces in defn.get('routers', {}).items():
        for iname, cfg in rfaces.items():
            net = ipaddress.IPv4Network(f"{cfg['address']}/{cfg['mask']}", strict=False)
            cidr = f"{net.network_address}/{net.prefixlen}"
            cfg.update({'node': rname, 'name': f"{rname}_{iname}", 'cost': cfg.get('cost', 1)})
            subnets.setdefault(cidr, []).append(cfg)
    # Hosts - exactly like emulation.py  
    for hname, hfaces in defn.get('hosts', {}).items():
        for iname, cfg in hfaces.items():
            net = ipaddress.IPv4Network(f"{cfg['address']}/{cfg['mask']}", strict=False)
            cidr = f"{net.network_address}/{net.prefixlen}"
            # Find router in same subnet for default gateway
            router_addr = None
            if cidr in subnets:
                for existing in subnets[cidr]:
                    if existing['node'].startswith('r'):
                        router_addr = existing['address']
                        break
            cfg.update({'node': hname, 'name': f"{hname}_{iname}", 'cost': 1,
                        'defaultrouter': router_addr})
            subnets[cidr].append(cfg)
    return subnets


class BasicTopo(Topo):
    def __init__(self, defn, subnets, cost_map):
        super().__init__()
        self.defn = defn
        self.subnets = subnets
        self.cost_map = cost_map
        self._switch_count = 0
        # Add routers
        for r in defn.get('routers', {}):
            self.addNode(r, cls=LinuxRouter, ip=None)
        # Add hosts  
        for h, ifaces in defn.get('hosts', {}).items():
            self.addHost(h, ip=None)
        # Add links per subnet - following emulation.py pattern
        for _, subnet in subnets.items():
            if len(subnet) == 2:
                self._link_pair(subnet)
            else:
                self._link_multi(subnet)
    
    def _link_pair(self, subnet):
        """Create point-to-point link following emulation.py pattern."""
        iface1, iface2 = subnet
        cost = max(iface1['cost'], iface2['cost'])
        # Record forwarding info - use peer's address as next hop
        key1 = f"{iface1['node']}_{iface2['node']}"
        key2 = f"{iface2['node']}_{iface1['node']}"
        self.cost_map[key1] = {'cost': cost, 'intf': iface2['address']}
        self.cost_map[key2] = {'cost': cost, 'intf': iface1['address']}
        # Add Mininet link with proper interface names and IPs
        self.addLink(
            iface1['node'], iface2['node'],
            intfName1=iface1['name'], params1={'ip': self._format_ip(iface1)},
            intfName2=iface2['name'], params2={'ip': self._format_ip(iface2)}
        )
    
    def _link_multi(self, subnet):
        """Create multi-node subnet using switch following emulation.py pattern."""
        # Create a switch to represent multi-node subnet
        sw = f"s{self._switch_count}"
        self._switch_count += 1
        self.addSwitch(sw)
        cost = max(iface['cost'] for iface in subnet)
        
        # Connect each interface to switch
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
    
    @staticmethod
    def _format_ip(iface):
        """Format IP address with CIDR prefix."""
        network = ipaddress.IPv4Network(f"{iface['address']}/{iface['mask']}", strict=False)
        return f"{iface['address']}/{network.prefixlen}"


def _gateway(defn, host):
    """Find the router attached to the same subnet as host."""
    subnets = build_subnets(defn)
    for ends in subnets.values():
        nodes = [ep['node'] for ep in ends]
        if host in nodes:
            for ep in ends:
                if ep['node'] in defn.get('routers', {}):
                    return ep['node']
    return None


def generate_lp(defn, subnets, cost_map, demands):
    lines = ["Maximize", " obj: ratiomin", "Subject To"]
    # Capacity constraints
    for link, data in cost_map.items():
        u, v = link.split('_')
        cap = data['cost']
        terms = [f"f{u}{v}{i+1}" for i in range(len(demands))]
        lines.append(f" ccap{u}{v}: {' + '.join(terms)} <= {cap}")
    # r_star <= demanded rate
    for i, d in enumerate(demands):
        lines.append(f" crmax{i+1}: rstar{i+1} <= {d['rate']}")
    # r_star >= ratiomin * rate  (implicit coefficient syntax)
    for i, d in enumerate(demands):
        lines.append(f" cratio{i+1}: rstar{i+1} - {d['rate']} ratiomin >= 0")
    # Flow conservation at routers
    routers = list(defn.get('routers', {}).keys())
    for i, d in enumerate(demands):
        srcR = _gateway(defn, d['src'])
        dstR = _gateway(defn, d['dst'])
        for r in routers:
            pos_terms = []  # positive terms
            neg_terms = []  # negative terms
            for link in cost_map:
                u, v = link.split('_')
                if u == r:
                    pos_terms.append(f"f{u}{v}{i+1}")
                if v == r:
                    neg_terms.append(f"f{u}{v}{i+1}")
            
            # Build constraint left side: positive terms first, then negative terms
            constraint_parts = []
            if pos_terms:
                constraint_parts.extend(pos_terms)
            if neg_terms:
                for term in neg_terms:
                    constraint_parts.append(f"- {term}")
            
            # Ensure we have at least one term to avoid empty constraint
            if not constraint_parts:
                constraint_parts = ["0"]
            
            # Join terms properly - first term without +, others with + where needed
            # IMPORTANT: CPLEX LP format cannot start with negative term, add "0" if needed
            constraint_left = constraint_parts[0]
            if constraint_left.startswith("-"):
                constraint_left = "0 " + constraint_left
            
            for term in constraint_parts[1:]:
                if term.startswith("-"):
                    constraint_left += f" {term}"
                else:
                    constraint_left += f" + {term}"
            
            if r == srcR:
                # Source: outflow - inflow = demand, converted to two inequalities
                rhs_ge = f"- rstar{i+1} >= 0"
                rhs_le = f"- rstar{i+1} <= 0"
            elif r == dstR:
                # Destination: outflow - inflow = -demand, converted to two inequalities  
                rhs_ge = f"+ rstar{i+1} >= 0"
                rhs_le = f"+ rstar{i+1} <= 0"
            else:
                # Transit: outflow - inflow = 0
                rhs_ge = ">= 0"
                rhs_le = "<= 0"
            
            # Generate two inequalities to represent equality constraint
            lines.append(f" cflow{i+1}{r}ge: {constraint_left} {rhs_ge}")
            lines.append(f" cflow{i+1}{r}le: {constraint_left} {rhs_le}")
    # Bounds
    lines.append("Bounds")
    lines.append(" ratiomin >= 0")
    for i in range(len(demands)):
        lines.append(f" rstar{i+1} >= 0")
        for link in cost_map:
            u, v = link.split('_')
            lines.append(f" f{u}{v}{i+1} >= 0")
    lines.append("End")
    return "\n".join(lines)


def solve_lp(lp_text):
    with tempfile.NamedTemporaryFile('w', suffix='.lp', delete=False) as lpfile:
        lpfile.write(lp_text)
        lp_path = lpfile.name
    
    sol_path = lp_path + '.sol'
    try:
        result = subprocess.run(['glpsol', '--cpxlp', lp_path, '-o', sol_path], 
                              capture_output=True, text=True, check=True)
        print("GLPK solved successfully!")
    except subprocess.CalledProcessError as e:
        print(f"LP solving failed. Return code: {e.returncode}")
        print(f"GLPK stderr: {e.stderr}")
        print(f"GLPK stdout: {e.stdout}")
        print(f"LP file saved at: {lp_path}")
        return {}
    
    vals = {}
    with open(sol_path) as f:
        for line in f:
            # Parse GLPK table format: "   No. Column name  St   Activity ..."
            # Look for lines with column information containing variable names and their values
            parts = line.split()
            if len(parts) >= 4:
                # Try to match pattern: number, variable_name, status, activity_value
                if parts[0].isdigit() and not parts[1] in ['Row', 'Column']:
                    try:
                        var_name = parts[1]
                        activity_value = float(parts[3])
                        vals[var_name] = activity_value
                    except (ValueError, IndexError):
                        continue
    return vals


def reconstruct_path(defn, subnets, demands, sol, idx):
    graph = {}
    for var, val in sol.items():
        if var.startswith('f') and var.endswith(f"{idx+1}") and val > 1e-9:
            var_body = var[1:-1]
            if len(var_body) >= 4:
                u = var_body[:2]
                v = var_body[2:]
                graph.setdefault(u, []).append(v)
    srcR = _gateway(defn, demands[idx]['src'])
    dstR = _gateway(defn, demands[idx]['dst'])
    q = deque([[srcR]])
    visited = {srcR}
    while q:
        path = q.popleft()
        if path[-1] == dstR:
            return path
        for nei in graph.get(path[-1], []):
            if nei not in visited:
                visited.add(nei)
                q.append(path + [nei])
    return []


def configure_mpls(net, defn, subnets, cost_map, demands, sol):
    for idx in range(len(demands)):
        path = reconstruct_path(defn, subnets, demands, sol, idx)
        label = idx + 1
        info(f"Configuring MPLS for flow {label}: path {path}\n")
        for u, v in zip(path, path[1:]):
            intf = cost_map.get(f"{u}_{v}", {}).get('intf')
            if intf:
                r = net.get(u)
                r.cmd(f"ip link add link {intf} name {intf}.mpls type mpls")
                r.cmd(f"mpls label add dev {intf}.mpls inbound pop 1")
                r.cmd(f"mpls label add dev {intf} outbound push {label}")


def dijkstra_shortest_paths(graph, start):
    """Compute shortest paths using Dijkstra's algorithm."""
    n = len(graph)
    dist = [float('inf')] * n
    prev = [-1] * n
    dist[start] = 0
    unvisited = set(range(n))
    
    while unvisited:
        u = min(unvisited, key=lambda x: dist[x])
        if dist[u] == float('inf'):
            break
        unvisited.remove(u)
        
        for v in range(n):
            if v in unvisited and graph[u][v] < float('inf'):
                alt = dist[u] + graph[u][v]
                if alt < dist[v]:
                    dist[v] = alt
                    prev[v] = u
    return dist, prev


def compute_routes(defn, subnets, cost_map):
    """Compute routing tables for all routers using shortest path."""
    routers = list(defn.get('routers', {}).keys())
    n = len(routers)
    
    # Build adjacency matrix
    graph = [[float('inf')] * n for _ in range(n)]
    for i in range(n):
        graph[i][i] = 0
    
    for link, data in cost_map.items():
        u, v = link.split('_')
        if u.startswith('r') and v.startswith('r'):
            u_idx = int(u[1:]) - 1
            v_idx = int(v[1:]) - 1
            graph[u_idx][v_idx] = data['cost']
    
    routes = {}
    for i, router in enumerate(routers):
        dist, prev = dijkstra_shortest_paths(graph, i)
        routes[router] = {}
        
        # For each subnet, find next hop
        for net_cidr, subnet_ends in subnets.items():
            # Skip directly connected subnets
            if any(ep['node'] == router for ep in subnet_ends):
                continue
                
            # Find target routers in this subnet
            target_routers = [ep['node'] for ep in subnet_ends if ep['node'].startswith('r')]
            if not target_routers:
                continue
                
            # Find closest target router
            best_target = min(target_routers, key=lambda r: dist[int(r[1:]) - 1])
            target_idx = int(best_target[1:]) - 1
            
            # Find next hop
            if prev[target_idx] == -1:
                continue
            
            cur = target_idx
            while prev[cur] != i:
                cur = prev[cur]
            
            next_hop_router = f"r{cur + 1}"
            next_hop_key = f"{router}_{next_hop_router}"
            if next_hop_key in cost_map:
                routes[router][net_cidr] = cost_map[next_hop_key]['intf']
    
    return routes


def configure_network(net, defn, subnets, cost_map):
    """Configure IP addresses, routing tables, and default routes."""
    # Set host default routes
    for host_name, host_ifaces in defn.get('hosts', {}).items():
        host = net.get(host_name)
        # Find the router in the same subnet
        host_addr = host_ifaces['eth0']['address']
        host_mask = host_ifaces['eth0']['mask']
        host_net = ipaddress.IPv4Network(f"{host_addr}/{host_mask}", strict=False)
        
        for net_cidr, subnet_ends in subnets.items():
            if str(host_net) == net_cidr:
                for ep in subnet_ends:
                    if ep['node'].startswith('r'):
                        gateway = ep['address']
                        info(f"*** Setting default route on {host_name} via {gateway}\n")
                        host.cmd(f"ip route add default via {gateway} dev {host_name}_eth0")
                        break
                break
    
    # Configure router routes
    routes = compute_routes(defn, subnets, cost_map)
    for router_name, router_routes in routes.items():
        router = net.get(router_name)
        for dest_net, next_hop in router_routes.items():
            info(f"*** {router_name}: add route {dest_net} via {next_hop}\n")
            router.cmd(f"ip route add {dest_net} via {next_hop}")


def configure_network_emulation_style(net, defn, subnets, cost_map):
    """Configure network following emulation.py approach exactly."""
    import heapq
    
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
        """For a given router index, find next-hop interfaces to each subnet."""
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

    # Build cost graph exactly like emulation.py
    routers = list(defn['routers'].keys())
    n = len(routers)
    graph = [[0 if i == j else cost_map.get(f'r{i+1}_r{j+1}', {}).get('cost', float('inf'))
              for j in range(n)] for i in range(n)]
    
    # Setup host default routes exactly like emulation.py
    for h, infs in defn['hosts'].items():
        info(f'*** Setting default route on host {h}\n')
        hnode = net.get(h)
        gw = infs['eth0']['defaultrouter']
        hnode.cmd(f'ip route add default via {gw} dev {h}_eth0')
    
    # Setup router static routes exactly like emulation.py
    for idx, r in enumerate(routers):
        rnode = net.get(r)
        routes = shortest_path_subnets(subnets, idx, graph, cost_map)
        for dest, via in routes.items():
            info(f'*** {r}: add route {dest} via {via}\n')
            rnode.cmd(f'ip route add {dest} via {via}')


def parse_args():
    p = argparse.ArgumentParser(description='Best Goodput Emulation')
    p.add_argument('definition', help='YAML topology+demands file')
    p.add_argument('-p', '--print', action='store_true', help='print optimal goodput per flow')
    p.add_argument('-l', '--lp', action='store_true', help='output CPLEX LP model')
    return p.parse_args()

if __name__ == '__main__':
    args = parse_args()
    with open(args.definition) as f:
        defn = yaml.safe_load(f)
    setLogLevel('info')
    # Build network definitions
    subnets = build_subnets(defn)
    cost_map = {}
    # Create topology (cost_map will be populated in BasicTopo)
    topo = BasicTopo(defn, subnets, cost_map)
    # Demand flows
    demands = defn.get('demands', [])
    # Generate LP model
    lp_text = generate_lp(defn, subnets, cost_map, demands)
    if args.lp:
        print(lp_text)
        exit(0)
    # Solve LP via GLPK
    sol = solve_lp(lp_text)
    if args.print:
        for idx, d in enumerate(demands):
            goodput = sol.get(f'rstar{idx+1}', 0)
            print(f"The best goodput for flow demand #{idx+1} is {goodput:.0f} Mbps")
        exit(0)
    # Emulation mode: start Mininet and configure network
    net = Mininet(topo=topo)
    net.start()
    # Configure network using emulation.py approach
    configure_network_emulation_style(net, defn, subnets, cost_map)
    configure_mpls(net, defn, subnets, cost_map, demands, sol)
    CLI(net)
    net.stop()
