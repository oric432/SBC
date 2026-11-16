interface MediaPeerCall {
  id: string;
  sipCallId: string;
  mediaPeerCallId?: string | null;
}

export interface DisplayedCall<T> {
  call: T;
  child: boolean;
}

export function groupMediaPeerCalls<T extends MediaPeerCall>(
  calls: T[],
): DisplayedCall<T>[] {
  const callsBySipCallId = new Map(calls.map((call) => [call.sipCallId, call]));
  const emittedCallIds = new Set<string>();
  const displayedCalls: DisplayedCall<T>[] = [];

  for (const call of calls) {
    if (emittedCallIds.has(call.id)) continue;

    displayedCalls.push({ call, child: false });
    emittedCallIds.add(call.id);

    const peer = call.mediaPeerCallId
      ? callsBySipCallId.get(call.mediaPeerCallId)
      : undefined;
    if (peer && !emittedCallIds.has(peer.id)) {
      displayedCalls.push({ call: peer, child: true });
      emittedCallIds.add(peer.id);
    }
  }

  return displayedCalls;
}
