export const getStore = () => {
  const globalStore = global as any;
  if (!globalStore.sessionMap) {
    globalStore.sessionMap = new Map<string, { sessionid: string; timestamp: number }>();
  }
  return globalStore.sessionMap as Map<string, { sessionid: string; timestamp: number }>;
};
