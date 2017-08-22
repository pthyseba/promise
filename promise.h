#include <future>
#include <functional>
#include <string>
#include <mutex>
#include <condition_variable>
#include <tuple>
#include <vector>
#include <iostream>

/*
   template class Promise: non-void template return type

     Executor: takes (resolve, reject) as parametsr, returns void, must call resolve with result or reject with reason
     .then()
        returns another Promise (possibly with different retutn type; in that case new type must be convertible to return type of original promise)
        takes a handler function to be executed when orignal promise resolves as first parameter, and handler to be executed when orignal promise is rejected as second parameter
           - handler takes const ref to original promise return type as first argument
           - handler either 
                - returns void and takes two extra parameters (resolve, reject); handler code is executed synchronously when original promise resolves and handler must call either resolve or reject 
                - returns a Promise (possibly with another template parameter type) and takes no extra parameters
 */

static std::atomic<int> promiseCount(0);

namespace {
  void PromiseDebug(std::ostream& aOstream, const std::string& aMessage)
  {
    #ifdef PROMISE_DEBUG
    aOstream << aMessage << std::endl;
    #endif
  }
}

namespace NPromise {

  enum class PromiseState { PENDING = 0, FULFILLED, REJECTED };

  template<typename taResolvedType>
  struct PromiseStateHolder;

  template<>
  struct PromiseStateHolder<void>
  {
    PromiseState iState;
    std::string iReason;
    std::mutex iMutex;
    std::condition_variable iConditionVariable;
  };

  template<typename taResolvedType>
  struct PromiseStateHolder : public PromiseStateHolder<void>
  {
    //PromiseState iState;
    taResolvedType iResult;
    //std::string iReason;
    //std::mutex iMutex;
    //std::condition_variable iConditionVariable;
  };

  template<typename taResolvedType>
  class PromiseBase
  {

  };

  

  template<typename taResolvedType> class Promise;

  template<>
    class Promise<void> 
  {
  private:

  };

  template<typename taResolvedType>
  class Promise : public Promise<void>
  {
  public:
    typedef std::function<void(const taResolvedType&)> TResolver;
    typedef std::function<void(const std::string&)> TRejecter;
    typedef std::function<void(const TResolver&, const TRejecter&)> TExecutor;
  
    explicit Promise(const TExecutor& aExecutor)
    {
      iStatePtr = std::make_shared<PromiseStateHolder<taResolvedType>>();
      iCount = promiseCount++;
      std::cout << "Promise " << iCount << " owns " << &(iStatePtr->iMutex) << std::endl;
      // Write state into state ptr
      TResolver r = [ptr=this->iStatePtr](const taResolvedType& aResult)
      { 
	std::unique_lock<std::mutex> l(ptr->iMutex);
        if (ptr->iState != PromiseState::PENDING)
          return;
        ptr->iResult = aResult; 
        ptr->iState = PromiseState::FULFILLED;
        l.unlock();
        ptr->iConditionVariable.notify_all();
      };
      TRejecter e = [ptr=this->iStatePtr](const std::string& aReason)
      {
        std::unique_lock<std::mutex> l(ptr->iMutex);
        if (ptr->iState != PromiseState::PENDING)
          return;
        ptr->iReason = aReason; 
        ptr->iState = PromiseState::REJECTED;
        l.unlock();
        ptr->iConditionVariable.notify_all();
      }; 

      // Pass functions by value
      TExecutor wrappedExecutor = [aExecutor](TResolver aResolver, TRejecter aRejecter)
      {
	try 
        {
	  aExecutor(aResolver, aRejecter);
	}
        catch(...)
        {
	  aRejecter("Unexpected exception thrown!");
	}
      };

      iExecutorFuture = std::async(std::launch::async, wrappedExecutor, r, e);  
    }

    Promise(Promise<taResolvedType>&& aOther)
    {
      iCount = promiseCount++;
      std::lock_guard<std::mutex> l(aOther.iStatePtr->iMutex);
      iExecutorFuture = std::move(aOther.iExecutorFuture);
      iStatePtr = std::move(aOther.iStatePtr);
      std::cout << "in move constructor" << std::endl;
    }

    Promise(const Promise<taResolvedType>& aOther)
    {
      iCount = promiseCount++;
      std::lock_guard<std::mutex> l(aOther.iStatePtr->iMutex);
      std::cout << "in copy constructor" << std::endl;
      iStatePtr = aOther.iStatePtr;
      iExecutorFuture = aOther.iExecutorFuture;
    }

    Promise<taResolvedType>& operator=(const Promise<taResolvedType>& aOther)
    {
      if (this == &aOther)
        return;
    
      iCount = promiseCount++;
      std::lock_guard<std::mutex> l(aOther.iStatePtr->iMutex);
      iStatePtr = aOther.iStatePtr;
      iExecutorFuture = aOther.iExecutorFuture;
      std::cout << "in copy assignment operator" << std::endl;
      return *this;
    }

    Promise<taResolvedType>& operator=(Promise<taResolvedType>&& aOther)
    {
      if (this == &aOther)
        return;
    
      iCount = promiseCount++;
      std::lock_guard<std::mutex> l(aOther.iStatePtr->iMutex);
      iStatePtr = std::move(aOther.iStatePtr);
      iExecutorFuture = std::move(aOther.iExecutorFuture);
      std::cout << "in move assignment operator" << std::endl;
      return *this;
    }
 
    template<typename taNewResolvedType = taResolvedType>
    Promise<taNewResolvedType> then(const std::function<void(const taResolvedType&, const typename Promise<taNewResolvedType>::TResolver&, 
                                                             const typename Promise<taNewResolvedType>::TRejecter&)>& aResolveHandler,
                                    const std::function<void(const std::string&, const typename Promise<taNewResolvedType>::TResolver&,
                                                             const typename Promise<taNewResolvedType>::TRejecter&)>& aRejectHandler = nullptr) const
    {
      // New executor: wait for this promise to resolve, then execute resolve or reject handler
      typename Promise<taNewResolvedType>::TExecutor e = 
        [ptr=this->iStatePtr, aResolveHandler, aRejectHandler, count=this->iCount](typename Promise<taNewResolvedType>::TResolver aResolver, typename Promise<taNewResolvedType>::TRejecter aRejecter)
        { 
          std::cout << "Then executor " << count << ": waiting to lock " << &(ptr->iMutex) << std::endl;
	  std::unique_lock<std::mutex> l(ptr->iMutex);
          ptr->iConditionVariable.wait(l, [ptr](){return ptr->iState != PromiseState::PENDING;});
          std::cout << "Then executor " << count << ": locked " << &(ptr->iMutex) << std::endl;
          if (ptr->iState == PromiseState::FULFILLED)
	  {
            try 
	    {
              aResolveHandler(ptr->iResult, aResolver, aRejecter);
            }
            catch(...)
	    {
	      aRejecter("Unexpected exception thrown by aResolveHandler!");
	    }
	  }
          else if(aRejectHandler)
	  {
	    // Original promise rejected
            try 
	    {
              aRejectHandler(ptr->iReason, aResolver, aRejecter);
            }
            catch(...)
	    {
	      aRejecter("Unexpected exception thrown by aRejectHandler!");
	    }
	  }
          l.unlock();
          std::cout << "Then executor: unlocked " << &(ptr->iMutex) << std::endl;
        };
   
      return Promise<taNewResolvedType>(e);
    }

    template<typename taNewResolvedType>
    Promise<taNewResolvedType> then(const std::function<taNewResolvedType(const taResolvedType&)>& aHandler) const
    {
      // New executor: wait for this promise to resolve, then execute handler
      typename Promise<taNewResolvedType>::TExecutor e = 
        [ptr=this->iStatePtr, aHandler, count=this->iCount](const typename Promise<taNewResolvedType>::TResolver& aResolver,
							    const typename Promise<taNewResolvedType>::TRejecter& aRejecter)
        { 
          std::cout << "Then executor " << count << ": waiting to lock " << &(ptr->iMutex) << std::endl;
	  std::unique_lock<std::mutex> l(ptr->iMutex);
          ptr->iConditionVariable.wait(l, [ptr](){return ptr->iState != PromiseState::PENDING;});
          std::cout << "Then executor " << count << ": locked " << &(ptr->iMutex) << std::endl;
	  if (ptr->iState == PromiseState::REJECTED)
	  {
	    std::cout << "returning" << std::endl;
	    // We don't handle rejected promises
            return;
	  }
          l.unlock();
          std::cout << "Then executor: unlocked " << &(ptr->iMutex) << std::endl;
          try 
          {
	    const taNewResolvedType& newResult = aHandler(ptr->iResult);
            aResolver(newResult);
	  }
          catch(...) 
          {
	    aRejecter("Unexpected exception thrown!");
	  }
          /*
          l.unlock();
          std::cout << "Then executor: unlocked " << &(ptr->iMutex) << std::endl;
          // TODO: handle rejected state as well
          // call inside l-lock()
          aResolver(newResult); 
          */
        };
   
      return Promise<taNewResolvedType>(e);
    }

    template<typename taNewResolvedType>
    Promise<taNewResolvedType> then(const std::function<Promise<taNewResolvedType>(const taResolvedType&)>& aHandler) const
    {
      typename Promise<taNewResolvedType>::TExecutor e = 
	[ptr=this->iStatePtr, aHandler, count=this->iCount](const typename Promise<taNewResolvedType>::TResolver& aResolver, const typename Promise<taNewResolvedType>::TRejecter&)
        { 
	  std::cout << "Then promise executor " << count << ": waiting to lock " << &(ptr->iMutex) << std::endl;
	  std::unique_lock<std::mutex> l(ptr->iMutex);
          ptr->iConditionVariable.wait(l, [ptr](){return ptr->iState == PromiseState::FULFILLED;});
          std::cout << "Then promise executor " << count << ": locked " << &(ptr->iMutex) << std::endl;
          // TODO: wrap  with try/catch?
	  Promise<taNewResolvedType> resultPromise = aHandler(ptr->iResult);
          l.unlock();
          // TODO: handle rejected state as well

          // Resolve this promise when promise returned by handler resolves
          std::function<int(const taNewResolvedType& aNextResult)> helper =  
	  [aResolver](const taNewResolvedType& aNextResult)
          { 
            aResolver(aNextResult); // conversion to taResolvedType?
            return 0;
          };
          resultPromise.then(helper); 
        };

      return Promise<taNewResolvedType>(e);
    }

    bool isPending() const 
    { 
      std::lock_guard<std::mutex> l(iStatePtr->iMutex);
      return iStatePtr->iState == PromiseState::PENDING; 
    }
 
    bool isFulfilled() const 
    {
      std::lock_guard<std::mutex> l(iStatePtr->iMutex);
      return iStatePtr->iState == PromiseState::FULFILLED; 
    }
  
    bool isRejected() const 
    {
      std::lock_guard<std::mutex> l(iStatePtr->iMutex);
      return iStatePtr->iState == PromiseState::REJECTED; 
    }

    taResolvedType GetResult() const
    { 
      std::cout << " GetResult: locking " << &(iStatePtr->iMutex) << std::endl;
      std::lock_guard<std::mutex> l(iStatePtr->iMutex);
      std::cout << " GetResult: locked " << &(iStatePtr->iMutex) << std::endl;
      return iStatePtr->iResult; 
    }
 
    std::string GetReason() const 
    { 
      std::lock_guard<std::mutex> l(iStatePtr->iMutex);
      return iStatePtr->iReason; 
    }

  private:
    std::shared_future<void> iExecutorFuture;
    int iCount;
    std::shared_ptr<PromiseStateHolder<taResolvedType>> iStatePtr;
  };

  

  /*
  template<>
  class Promise<void>
  {
  public:
    typedef std::function<void()> TResolver;
    typedef std::function<void(const std::string&)> TRejecter;
    typedef std::function<void(TResolver, TRejecter)> TExecutor;
  
    Promise(TExecutor);
  
  private:
    void resolve();
    void reject(const std::string&);
    PromiseState iState;
  };
  */

} // namespace Promise

  // helpers for NPromise::All

  template<int taResultIndex, typename taThisResultType, typename... taTupleTypes>
  void AttachOne(const NPromise::Promise<taThisResultType>& aPromise, std::mutex& aMutex, std::condition_variable& aCondition, int& aCounter, std::tuple<taTupleTypes...>& aTuple, std::string& aReason, std::vector<NPromise::Promise<int>>& aPromiseStore )
  {
    std::cout << "Attacher " << taResultIndex << std::endl; 
    // lambda that stores result in nth element of aTuple
    auto& resultStore = std::get<taResultIndex>(aTuple);

    /*
    const std::function<void(const taResolvedType&, const typename Promise<taNewResolvedType>::TResolver&, 
                                                             const typename Promise<taNewResolvedType>::TRejecter&)>& aResolveHandler,
                                    const std::function<void(const std::string&, const typename Promise<taNewResolvedType>::TResolver&,
                                                             const typename Promise<taNewResolvedType>::TRejecter&)>& aRejectHandler


     */
    std::function<void(const taThisResultType&, const typename NPromise::Promise<int>::TResolver&, 
                       const typename NPromise::Promise<int>::TRejecter& )> resultHandler =
      [&resultStore, &aMutex, &aCondition, &aCounter](const taThisResultType& aResult, const typename NPromise::Promise<int>::TResolver&, 
						      const typename NPromise::Promise<int>::TRejecter&)
      {
	std::unique_lock<std::mutex> l(aMutex);
	resultStore = aResult;
	aCounter++;
	std::cout << "Resolved promise " << taResultIndex << std::endl;
	l.unlock();
	aCondition.notify_one();
      };

    std::function<void(const std::string&, const typename NPromise::Promise<int>::TResolver&, 
		      const typename NPromise::Promise<int>::TRejecter&)> rejectHandler =
      [&aReason, &aMutex, &aCondition, &aCounter](const std::string& aRejectReason, const typename NPromise::Promise<int>::TResolver&, 
						  const typename NPromise::Promise<int>::TRejecter&)
      {
	std::unique_lock<std::mutex> l(aMutex);
	aReason = aRejectReason;
	aCounter++;
	std::cout << "Rejected promise " << taResultIndex << std::endl;
	l.unlock();
	aCondition.notify_one();
      };
    

    // These push_backs are executed in sequence, so no lock required
    aPromiseStore.push_back(std::move(aPromise.template then<int>(resultHandler, rejectHandler)));
    std::cout << "Attacher " << taResultIndex << "end" << std::endl;
  }

  template<int taIndex, int taMax, typename... taResolvedTypes>
  struct Attacher
  {
    static void Apply(std::mutex& aMutex, std::condition_variable& aCondition, int& aCounter, std::tuple<taResolvedTypes...>& aTuple, std::string& aReason,
                      const std::tuple<const NPromise::Promise<taResolvedTypes>&...>& aPromises, std::vector<NPromise::Promise<int>>& aPromiseStore)
    {
      AttachOne<taIndex, typename std::tuple_element<taIndex, std::tuple<taResolvedTypes...>>::type, taResolvedTypes...>(std::get<taIndex>(aPromises), aMutex, aCondition, aCounter, aTuple, aReason, aPromiseStore);
      Attacher<taIndex+1, taMax, taResolvedTypes...>::Apply(aMutex, aCondition, aCounter, aTuple, aReason, aPromises, aPromiseStore); 
    }
  };

  template<int n, typename... taTypes>
  struct Attacher<n, n, taTypes...>
  {
    static void Apply(std::mutex& aMutex, std::condition_variable& aCondition, int& aCounter, std::tuple<taTypes...>& aTuple, std::string& aReason,
                      const std::tuple<const NPromise::Promise<taTypes>&...>& aPromises, std::vector<NPromise::Promise<int>>& aPromiseStore)
    { // NOP
    }
  };


namespace NPromise {
  // template, specialize for 1 and more promises?
  template<typename... taResolvedTypes>
  Promise<std::tuple<taResolvedTypes...>> All(const Promise<taResolvedTypes>&... aPromises)
  {
    constexpr int n = sizeof...(aPromises);
  
    // problem:
    typename Promise<std::tuple<taResolvedTypes...>>::TExecutor e = 
      [params = std::forward_as_tuple(aPromises...)](typename Promise<std::tuple<taResolvedTypes...>>::TResolver aResolver, typename Promise<std::tuple<taResolvedTypes...>>::TRejecter aRejecter)
      {
	std::mutex m;
	std::condition_variable c;
	std::tuple<taResolvedTypes...> result; 
        // struct to hold n helper Promises, one for each input promise
        // moved in here so that AttachOne, and therefore All, returns immediately
	std::vector<NPromise::Promise<int>> promises;
        // prevent Promise copy constructor being called due to vector memory allocation upon push_back()
        promises.reserve(n);
	int counter = 0;
        // TODO: cannot reject with empty string? Add boolean var instead
	std::string reason("");

	Attacher<0, n, taResolvedTypes...>::Apply(m, c, counter, result, reason, params, promises);

	// wait for all promises to resolve
        // TODO pass flag for failed promise and check that one as well
	std::unique_lock<std::mutex> l(m);
	c.wait(l, [&counter, &reason](){return (counter == n) || (reason.size() > 0);});
	std::cout << "Resolving All" << std::endl;
        if (reason.size() == 0)
	{
	  aResolver(result);        
        }
        else 
	{
	  aRejecter(reason);
	}
      };

    return Promise<std::tuple<taResolvedTypes...>>(e);
  }


} // namespace Promise
